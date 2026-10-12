#include <unity.h>

#include <chrono>
#include <string>
#include <vector>

#include "../support/web_access_fakes.h"
#include "core/actions/action_bus.h"
#include "services/web_access/web_access_service.h"

using namespace cardputer_hub;
using namespace cardputer_hub::connectivity;
using namespace cardputer_hub::services;
using namespace cardputer_hub::test_support;
using namespace std::chrono_literals;

namespace {

class EchoApp final : public IWebApp {
  public:
    std::string_view id() const override { return "echo"; }
    std::string_view title() const override { return "ECHO"; }
    std::string summaryJson() override { return R"({"count":3})"; }
    HttpResponse handle(const HttpRequest& request, std::string_view subpath) override {
        ++calls;
        lastSubpath = std::string(subpath);
        lastBody = request.body;
        return webJsonResponse(200, R"({"ok":true})");
    }
    void reset() override { ++resets; }

    int calls = 0;
    int resets = 0;
    std::string lastSubpath;
    std::string lastBody;
};

struct Harness {
    Harness() : storage(storageAdapter), web(accessPoint, server, storage, random) {
        (void)web.addApp(app);
    }

    FakeAccessPoint accessPoint;
    FakeHttpServer server;
    MapStorage storageAdapter;
    core::Storage storage;
    CountingRandom random;
    EchoApp app;
    WebAccessService web;

    // Sends one request through update() and returns its response.
    HttpResponse send(HttpRequest request) {
        const auto id = server.push(std::move(request));
        web.update(0ms);
        const auto* response = server.responseFor(id);
        TEST_ASSERT_NOT_NULL(response);
        return *response;
    }

    HttpResponse signIn(const std::string& login, const std::string& password) {
        HttpRequest request;
        request.method = HttpMethod::Post;
        request.path = "/api/session";
        request.requestMarker = "1";
        request.body = R"({"login":")" + login + R"(","password":")" + password + R"("})";
        return send(request);
    }

    // Signs in and returns the Cookie header value for later requests.
    std::string cookie() {
        const auto response = signIn("admin", web.status().password);
        TEST_ASSERT_EQUAL_UINT16(204, response.status);
        const auto setCookie = responseHeader(response, "Set-Cookie");
        TEST_ASSERT_TRUE(setCookie.has_value());
        return setCookie->substr(0, setCookie->find(';'));
    }

    HttpResponse get(const std::string& path, const std::string& cookieHeader) {
        HttpRequest request;
        request.path = path;
        request.cookie = cookieHeader;
        return send(request);
    }
};

void test_service_is_off_until_enabled() {
    Harness harness;
    harness.web.update(10ms);
    TEST_ASSERT_EQUAL(WebAccessState::Off, harness.web.status().state);
    TEST_ASSERT_EQUAL_INT(0, harness.accessPoint.starts);
    TEST_ASSERT_EQUAL_INT(0, harness.server.starts);
}

void test_enable_generates_and_stores_credentials() {
    Harness harness;
    TEST_ASSERT_EQUAL(WebAccessResult::Success, harness.web.setEnabled(true));

    const auto& status = harness.web.status();
    TEST_ASSERT_EQUAL(WebAccessState::Running, status.state);
    TEST_ASSERT_EQUAL_STRING("HUB-0001", status.ssid.c_str());
    TEST_ASSERT_EQUAL_size_t(12, status.password.size());
    const std::string alphabet = "abcdefghijkmnpqrstuvwxyz23456789";
    for (const char character : status.password)
        TEST_ASSERT_TRUE(alphabet.find(character) != std::string::npos);
    TEST_ASSERT_TRUE(harness.accessPoint.running);
    TEST_ASSERT_EQUAL_STRING(status.ssid.c_str(), harness.accessPoint.last.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(status.password.c_str(), harness.accessPoint.last.passphrase.c_str());
    TEST_ASSERT_TRUE(harness.server.isRunning);
    TEST_ASSERT_EQUAL_INT(1, harness.storageAdapter.writes);
}

void test_stored_credentials_are_reused() {
    Harness first;
    (void)first.web.setEnabled(true);
    const auto ssid = first.web.status().ssid;
    const auto password = first.web.status().password;

    Harness second;
    second.storageAdapter.values = first.storageAdapter.values;
    second.random.next = 200;
    (void)second.web.setEnabled(true);

    TEST_ASSERT_EQUAL_STRING(ssid.c_str(), second.web.status().ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(password.c_str(), second.web.status().password.c_str());
    TEST_ASSERT_EQUAL_INT(0, second.storageAdapter.writes);
}

void test_damaged_stored_credentials_are_replaced() {
    Harness harness;
    harness.storageAdapter.values["web/access"] = {9, 9, 9};
    (void)harness.web.setEnabled(true);
    TEST_ASSERT_EQUAL(WebAccessState::Running, harness.web.status().state);
    TEST_ASSERT_TRUE(validWifiAccessPointConfig(harness.accessPoint.last));
    TEST_ASSERT_EQUAL_INT(1, harness.storageAdapter.writes);
}

void test_unsaved_credentials_stay_until_restart() {
    Harness harness;
    harness.storageAdapter.failWrites = true;
    (void)harness.web.setEnabled(true);
    const auto ssid = harness.web.status().ssid;
    const auto password = harness.web.status().password;
    (void)harness.web.setEnabled(false);
    (void)harness.web.setEnabled(true);
    TEST_ASSERT_EQUAL_STRING(ssid.c_str(), harness.web.status().ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(password.c_str(), harness.web.status().password.c_str());
}

void test_access_point_failure_enters_error() {
    Harness harness;
    harness.accessPoint.failStart = true;
    TEST_ASSERT_EQUAL(WebAccessResult::AccessPointError, harness.web.setEnabled(true));
    TEST_ASSERT_EQUAL(WebAccessState::Error, harness.web.status().state);
    TEST_ASSERT_EQUAL_INT(0, harness.server.starts);

    harness.accessPoint.failStart = false;
    TEST_ASSERT_EQUAL(WebAccessResult::Success, harness.web.setEnabled(true));
    TEST_ASSERT_EQUAL(WebAccessState::Running, harness.web.status().state);
}

void test_server_failure_stops_access_point() {
    Harness harness;
    harness.server.failStart = true;
    TEST_ASSERT_EQUAL(WebAccessResult::ServerError, harness.web.setEnabled(true));
    TEST_ASSERT_EQUAL(WebAccessState::Error, harness.web.status().state);
    TEST_ASSERT_FALSE(harness.accessPoint.running);
    TEST_ASSERT_TRUE(harness.web.status().password.empty());
}

void test_repeated_enable_restarts_nothing() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    TEST_ASSERT_EQUAL(WebAccessResult::Success, harness.web.setEnabled(true));
    TEST_ASSERT_EQUAL_INT(1, harness.accessPoint.starts);
    TEST_ASSERT_EQUAL_INT(1, harness.server.starts);
}

void test_action_enables_and_disables() {
    Harness harness;
    core::ActionBus actions;
    TEST_ASSERT_EQUAL(core::RegistrationResult::Registered,
                      actions.registerHandler(webAccessSetEnabledActionId, harness.web));
    TEST_ASSERT_EQUAL(core::DispatchResult::Handled,
                      actions.dispatch({webAccessSetEnabledActionId, "test", {{"enabled", true}}}));
    TEST_ASSERT_EQUAL(WebAccessState::Running, harness.web.status().state);
    TEST_ASSERT_EQUAL(
        core::DispatchResult::Handled,
        actions.dispatch({webAccessSetEnabledActionId, "test", {{"enabled", false}}}));
    TEST_ASSERT_EQUAL(WebAccessState::Off, harness.web.status().state);
    TEST_ASSERT_FALSE(harness.accessPoint.running);
    TEST_ASSERT_FALSE(harness.server.isRunning);
    TEST_ASSERT_EQUAL(core::DispatchResult::Rejected,
                      actions.dispatch({webAccessSetEnabledActionId, "test", {}}));
}

void test_client_count_follows_access_point() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    harness.accessPoint.clients = 2;
    harness.web.update(WebAccessService::clientPollInterval);
    TEST_ASSERT_EQUAL_size_t(2, harness.web.status().clients);
}

void test_page_is_served_without_session() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto response = harness.get("/", "");
    TEST_ASSERT_EQUAL_UINT16(200, response.status);
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", response.contentType.c_str());
    TEST_ASSERT_FALSE(response.staticBody.empty());
    TEST_ASSERT_EQUAL_STRING("gzip", responseHeader(response, "Content-Encoding")->c_str());
    TEST_ASSERT_TRUE(responseHeader(response, "Content-Security-Policy").has_value());
    TEST_ASSERT_NOT_EQUAL(
        std::string::npos,
        responseHeader(response, "Content-Security-Policy")->find("manifest-src 'self'"));
    TEST_ASSERT_EQUAL_UINT16(404, harness.get("/favicon.ico", "").status);
}

void test_home_screen_files_are_served_without_session() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto manifest = harness.get("/manifest.webmanifest", "");
    TEST_ASSERT_EQUAL_UINT16(200, manifest.status);
    TEST_ASSERT_EQUAL_STRING("application/manifest+json", manifest.contentType.c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, responseText(manifest).find("\"standalone\""));
    for (const auto* path : {"/apple-touch-icon.png", "/icon-192.png", "/icon-512.png"}) {
        const auto icon = harness.get(path, "");
        TEST_ASSERT_EQUAL_UINT16(200, icon.status);
        TEST_ASSERT_EQUAL_STRING("image/png", icon.contentType.c_str());
        TEST_ASSERT_EQUAL_STRING("\x89PNG", responseText(icon).substr(0, 4).c_str());
        TEST_ASSERT_FALSE(responseHeader(icon, "Content-Encoding").has_value());
    }
    HttpRequest post;
    post.method = HttpMethod::Post;
    post.path = "/apple-touch-icon.png";
    TEST_ASSERT_EQUAL_UINT16(404, harness.send(post).status);
}

void test_sign_in_sets_session_cookie() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto response = harness.signIn("admin", harness.web.status().password);
    TEST_ASSERT_EQUAL_UINT16(204, response.status);
    const auto setCookie = responseHeader(response, "Set-Cookie");
    TEST_ASSERT_TRUE(setCookie.has_value());
    TEST_ASSERT_EQUAL_UINT(0, setCookie->find("hub_session="));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, setCookie->find("HttpOnly"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, setCookie->find("SameSite=Strict"));
    // 128 bits as hex.
    TEST_ASSERT_EQUAL_size_t(32, setCookie->find(';') - std::string("hub_session=").size());
}

void test_wrong_credentials_are_refused() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    TEST_ASSERT_EQUAL_UINT16(401, harness.signIn("admin", "wrongpassword").status);
    TEST_ASSERT_EQUAL_UINT16(401, harness.signIn("root", harness.web.status().password).status);
    HttpRequest malformed;
    malformed.method = HttpMethod::Post;
    malformed.path = "/api/session";
    malformed.requestMarker = "1";
    malformed.body = "login=admin";
    TEST_ASSERT_EQUAL_UINT16(400, harness.send(malformed).status);
}

void test_sign_in_throttles_after_failures() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    for (std::size_t attempt = 0; attempt < WebAccessService::signInAttempts; ++attempt)
        TEST_ASSERT_EQUAL_UINT16(401, harness.signIn("admin", "wrongpassword").status);
    TEST_ASSERT_EQUAL_UINT16(429, harness.signIn("admin", harness.web.status().password).status);
    harness.web.update(WebAccessService::signInLockout);
    TEST_ASSERT_EQUAL_UINT16(204, harness.signIn("admin", harness.web.status().password).status);
}

void test_api_requires_session() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    TEST_ASSERT_EQUAL_UINT16(401, harness.get("/api/hub", "").status);
    TEST_ASSERT_EQUAL_UINT16(401, harness.get("/api/hub", "hub_session=0123").status);
    TEST_ASSERT_EQUAL_UINT16(401, harness.get("/api/echo/x", "").status);
    TEST_ASSERT_EQUAL_INT(0, harness.app.calls);
}

void test_mutation_requires_marker() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto cookie = harness.cookie();
    HttpRequest request;
    request.method = HttpMethod::Put;
    request.path = "/api/echo/item";
    request.cookie = cookie;
    TEST_ASSERT_EQUAL_UINT16(403, harness.send(request).status);
    TEST_ASSERT_EQUAL_INT(0, harness.app.calls);

    HttpRequest signIn;
    signIn.method = HttpMethod::Post;
    signIn.path = "/api/session";
    signIn.body = R"({"login":"admin","password":")" + harness.web.status().password + R"("})";
    TEST_ASSERT_EQUAL_UINT16(403, harness.send(signIn).status);
}

void test_hub_lists_device_and_apps() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    harness.accessPoint.clients = 1;
    harness.web.update(WebAccessService::clientPollInterval);
    const auto response = harness.get("/api/hub", harness.cookie());
    TEST_ASSERT_EQUAL_UINT16(200, response.status);
    TEST_ASSERT_EQUAL_STRING("application/json", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING(
        R"({"device":{"network":"HUB-0001","clients":1,"address":"192.168.4.1"},)"
        R"("apps":[{"id":"echo","title":"ECHO","summary":{"count":3}}]})",
        response.body.c_str());
}

void test_app_requests_are_routed_with_subpath() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto cookie = harness.cookie();
    HttpRequest request;
    request.method = HttpMethod::Put;
    request.path = "/api/echo/records/7";
    request.cookie = cookie;
    request.requestMarker = "1";
    request.body = "{}";
    TEST_ASSERT_EQUAL_UINT16(200, harness.send(request).status);
    TEST_ASSERT_EQUAL_STRING("/records/7", harness.app.lastSubpath.c_str());
    TEST_ASSERT_EQUAL_UINT16(200, harness.get("/api/echo", cookie).status);
    TEST_ASSERT_EQUAL_STRING("", harness.app.lastSubpath.c_str());
    TEST_ASSERT_EQUAL_UINT16(404, harness.get("/api/echoes", cookie).status);
    TEST_ASSERT_EQUAL_UINT16(404, harness.get("/api/missing", cookie).status);
}

void test_sign_out_ends_session() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto cookie = harness.cookie();
    HttpRequest request;
    request.method = HttpMethod::Delete;
    request.path = "/api/session";
    request.cookie = cookie;
    request.requestMarker = "1";
    const auto response = harness.send(request);
    TEST_ASSERT_EQUAL_UINT16(204, response.status);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          responseHeader(response, "Set-Cookie")->find("Max-Age=0"));
    TEST_ASSERT_EQUAL_UINT16(401, harness.get("/api/hub", cookie).status);
}

void test_idle_session_expires() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto cookie = harness.cookie();
    harness.web.update(WebAccessService::sessionIdleTimeout - 1ms);
    TEST_ASSERT_EQUAL_UINT16(200, harness.get("/api/hub", cookie).status);
    harness.web.update(WebAccessService::sessionIdleTimeout);
    TEST_ASSERT_EQUAL_UINT16(401, harness.get("/api/hub", cookie).status);
}

void test_oldest_session_is_replaced_when_full() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    std::vector<std::string> cookies;
    for (std::size_t index = 0; index <= WebAccessService::maximumSessions; ++index) {
        cookies.push_back(harness.cookie());
        harness.web.update(1s);
    }
    TEST_ASSERT_EQUAL_UINT16(401, harness.get("/api/hub", cookies.front()).status);
    TEST_ASSERT_EQUAL_UINT16(200, harness.get("/api/hub", cookies.back()).status);
}

void test_stop_answers_waiting_request_and_clears_sessions() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    const auto cookie = harness.cookie();
    HttpRequest waiting;
    waiting.path = "/api/hub";
    waiting.cookie = cookie;
    const auto id = harness.server.push(waiting);
    harness.server.waiting = id;

    TEST_ASSERT_EQUAL(WebAccessResult::Success, harness.web.setEnabled(false));
    TEST_ASSERT_EQUAL(WebAccessState::Off, harness.web.status().state);
    TEST_ASSERT_FALSE(harness.server.isRunning);
    TEST_ASSERT_FALSE(harness.accessPoint.running);
    TEST_ASSERT_EQUAL_size_t(1, harness.server.ended.size());
    TEST_ASSERT_EQUAL_INT(1, harness.app.resets);
    TEST_ASSERT_TRUE(harness.web.status().password.empty());

    // A restart does not bring the old session back, and nothing is replayed.
    (void)harness.web.setEnabled(true);
    harness.web.update(0ms);
    TEST_ASSERT_NULL(harness.server.responseFor(id));
    TEST_ASSERT_EQUAL_UINT16(401, harness.get("/api/hub", cookie).status);
}

void test_server_loss_enters_error() {
    Harness harness;
    (void)harness.web.setEnabled(true);
    harness.server.isRunning = false;
    harness.web.update(0ms);
    TEST_ASSERT_EQUAL(WebAccessState::Error, harness.web.status().state);
    TEST_ASSERT_FALSE(harness.accessPoint.running);
}

} // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_service_is_off_until_enabled);
    RUN_TEST(test_enable_generates_and_stores_credentials);
    RUN_TEST(test_stored_credentials_are_reused);
    RUN_TEST(test_damaged_stored_credentials_are_replaced);
    RUN_TEST(test_unsaved_credentials_stay_until_restart);
    RUN_TEST(test_access_point_failure_enters_error);
    RUN_TEST(test_server_failure_stops_access_point);
    RUN_TEST(test_repeated_enable_restarts_nothing);
    RUN_TEST(test_action_enables_and_disables);
    RUN_TEST(test_client_count_follows_access_point);
    RUN_TEST(test_page_is_served_without_session);
    RUN_TEST(test_home_screen_files_are_served_without_session);
    RUN_TEST(test_sign_in_sets_session_cookie);
    RUN_TEST(test_wrong_credentials_are_refused);
    RUN_TEST(test_sign_in_throttles_after_failures);
    RUN_TEST(test_api_requires_session);
    RUN_TEST(test_mutation_requires_marker);
    RUN_TEST(test_hub_lists_device_and_apps);
    RUN_TEST(test_app_requests_are_routed_with_subpath);
    RUN_TEST(test_sign_out_ends_session);
    RUN_TEST(test_idle_session_expires);
    RUN_TEST(test_oldest_session_is_replaced_when_full);
    RUN_TEST(test_stop_answers_waiting_request_and_clears_sessions);
    RUN_TEST(test_server_loss_enters_error);
    return UNITY_END();
}
