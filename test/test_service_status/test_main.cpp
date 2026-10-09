#include <unity.h>

#include "../support/service_status_fixture.h"

#include "services/service_status/service_status_service.h"

namespace {
using namespace cardputer_hub;
using connectivity::ServiceStatusLevel;
using services::StatusProblem;
using services::StatusRoute;
using std::chrono::milliseconds;
using std::chrono::seconds;
using test_support::ServiceStatusFixture;
using test_support::statusBody;

void assertLevel(ServiceStatusLevel expected, ServiceStatusLevel actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(expected), static_cast<unsigned>(actual));
}

// Scenario: happy path over Wi-Fi.
void test_opening_checks_every_page_over_wifi_one_at_a_time() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.service.setActive(true);
    TEST_ASSERT_TRUE(fixture.service.snapshot().checking);
    TEST_ASSERT_EQUAL_UINT(1, fixture.http.urls.size());
    TEST_ASSERT_EQUAL_STRING(services::statusSources[0].url, fixture.http.urls[0].c_str());

    fixture.http.answer(200, statusBody("none", "All Systems Operational"));
    fixture.tick();
    assertLevel(ServiceStatusLevel::Operational, fixture.entry(0).level);
    TEST_ASSERT_TRUE(fixture.entry(0).route == StatusRoute::WiFi);
    TEST_ASSERT_EQUAL_STRING("All Systems Operational", fixture.entry(0).description.data());
    TEST_ASSERT_EQUAL_UINT(2, fixture.http.urls.size());
    TEST_ASSERT_EQUAL_STRING(services::statusSources[1].url, fixture.http.urls[1].c_str());

    fixture.http.answer(200, statusBody("minor", "Partially Degraded Service"));
    fixture.tick();
    fixture.http.answer(200, statusBody("major"));
    fixture.tick();
    fixture.http.answer(200, statusBody("maintenance"));
    fixture.tick();
    assertLevel(ServiceStatusLevel::Minor, fixture.entry(1).level);
    assertLevel(ServiceStatusLevel::Major, fixture.entry(2).level);
    assertLevel(ServiceStatusLevel::Maintenance, fixture.entry(3).level);
    TEST_ASSERT_FALSE(fixture.service.snapshot().checking);
    TEST_ASSERT_EQUAL_UINT(4, fixture.http.urls.size());
    TEST_ASSERT_TRUE(fixture.transport.statusRequests().empty());
}

void test_pages_are_checked_again_every_minute_while_open() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.service.setActive(true);
    fixture.answerRoundOverWifi("none");
    fixture.tick(seconds(59));
    TEST_ASSERT_EQUAL_UINT(4, fixture.http.urls.size());
    TEST_ASSERT_EQUAL_INT(59000, static_cast<int>(fixture.entry(3).sinceCheck.count()));
    fixture.tick(seconds(1));
    TEST_ASSERT_EQUAL_UINT(5, fixture.http.urls.size());
    TEST_ASSERT_TRUE(fixture.service.snapshot().checking);
}

void test_nothing_is_fetched_while_closed() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.connectCompanion();
    fixture.tick(seconds(300));
    TEST_ASSERT_TRUE(fixture.http.urls.empty());
    TEST_ASSERT_TRUE(fixture.transport.statusRequests().empty());

    fixture.service.setActive(true);
    fixture.service.setActive(false);
    TEST_ASSERT_EQUAL_INT(1, fixture.http.abandoned);
    TEST_ASSERT_FALSE(fixture.service.snapshot().checking);
    fixture.tick(seconds(300));
    TEST_ASSERT_EQUAL_UINT(1, fixture.http.urls.size());
}

// Scenario: unavailable dependency (no Wi-Fi) uses the Companion.
void test_without_wifi_the_companion_fetches_the_page() {
    ServiceStatusFixture fixture;
    fixture.connectCompanion();
    fixture.service.setActive(true);
    TEST_ASSERT_TRUE(fixture.http.urls.empty());
    const auto requests = fixture.transport.statusRequests();
    TEST_ASSERT_EQUAL_UINT(1, requests.size());
    char url[connectivity::companionMaxStatusUrlSize + 1]{};
    TEST_ASSERT_TRUE(connectivity::readServiceStatusRequest(requests[0], url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(services::statusSources[0].url, url);

    fixture.answerCompanion(ServiceStatusLevel::Minor, "Partially Degraded Service");
    assertLevel(ServiceStatusLevel::Minor, fixture.entry(0).level);
    TEST_ASSERT_TRUE(fixture.entry(0).route == StatusRoute::Companion);
    TEST_ASSERT_EQUAL_STRING("Partially Degraded Service", fixture.entry(0).description.data());
    TEST_ASSERT_EQUAL_UINT(2, fixture.transport.statusRequests().size());
}

// Scenario: in-flight failure over Wi-Fi falls back to the Companion.
void test_a_failed_wifi_fetch_falls_back_to_the_companion() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.connectCompanion();
    fixture.service.setActive(true);
    fixture.http.failNetwork();
    fixture.tick();
    TEST_ASSERT_EQUAL_UINT(1, fixture.transport.statusRequests().size());
    fixture.answerCompanion(ServiceStatusLevel::Operational);
    assertLevel(ServiceStatusLevel::Operational, fixture.entry(0).level);
    TEST_ASSERT_TRUE(fixture.entry(0).route == StatusRoute::Companion);
    // The next page tries Wi-Fi again first.
    TEST_ASSERT_EQUAL_UINT(2, fixture.http.urls.size());

    fixture.http.answer(503, "Service Unavailable");
    fixture.tick();
    fixture.failCompanion();
    assertLevel(ServiceStatusLevel::Unknown, fixture.entry(1).level);
    TEST_ASSERT_TRUE(fixture.entry(1).problem == StatusProblem::Unreachable);
    TEST_ASSERT_TRUE(fixture.entry(1).route == StatusRoute::Companion);
}

void test_wifi_failure_without_a_companion_marks_the_page_unreachable() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.service.setActive(true);
    fixture.http.failNetwork();
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.entry(0).problem == StatusProblem::Unreachable);
    TEST_ASSERT_TRUE(fixture.entry(0).route == StatusRoute::WiFi);
    fixture.http.answer(200, "<html>captive portal</html>");
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.entry(1).problem == StatusProblem::Unreadable);
    assertLevel(ServiceStatusLevel::Unknown, fixture.entry(1).level);
}

void test_without_any_connection_every_page_reports_no_connection() {
    ServiceStatusFixture fixture;
    fixture.service.setActive(true);
    TEST_ASSERT_FALSE(fixture.service.snapshot().checking);
    for (std::size_t index = 0; index < services::statusSources.size(); ++index) {
        TEST_ASSERT_TRUE(fixture.entry(index).checked);
        TEST_ASSERT_TRUE(fixture.entry(index).problem == StatusProblem::NoConnection);
    }
    TEST_ASSERT_TRUE(fixture.http.urls.empty());
}

// Scenario: reconnect. A Companion session lost mid-request is not replayed.
void test_companion_loss_during_a_request_fails_only_that_page() {
    ServiceStatusFixture fixture;
    fixture.connectCompanion();
    fixture.service.setActive(true);
    fixture.transport.incoming.push_back(
        test_support::companionWire(test_support::companionHello("2026-09-30 def5678")));
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.entry(0).problem == StatusProblem::Unreachable);
}

// Scenario: stale completion after the app closed.
void test_an_answer_after_closing_is_ignored() {
    ServiceStatusFixture fixture;
    fixture.connectCompanion();
    fixture.service.setActive(true);
    fixture.service.setActive(false);
    fixture.answerCompanion(ServiceStatusLevel::Critical);
    TEST_ASSERT_FALSE(fixture.entry(0).checked);

    fixture.service.setActive(true);
    fixture.answerCompanion(ServiceStatusLevel::Operational);
    assertLevel(ServiceStatusLevel::Operational, fixture.entry(0).level);
}

void test_an_abandoned_wifi_request_delays_the_next_page() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.service.setActive(true);
    fixture.service.setActive(false);
    fixture.http.holdsAbandoned = true;
    fixture.service.setActive(true);
    TEST_ASSERT_EQUAL_UINT(1, fixture.http.urls.size());
    fixture.tick();
    TEST_ASSERT_EQUAL_UINT(1, fixture.http.urls.size());
    fixture.http.holdsAbandoned = false;
    fixture.tick();
    TEST_ASSERT_EQUAL_UINT(2, fixture.http.urls.size());
    TEST_ASSERT_EQUAL_STRING(services::statusSources[0].url, fixture.http.urls[1].c_str());
}

void test_a_worse_incident_plays_one_fault_cue_per_round() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.service.setActive(true);
    // The first answer is not a change.
    fixture.answerRoundOverWifi("minor");
    TEST_ASSERT_TRUE(fixture.audioAdapter.clips.empty());

    fixture.tick(seconds(60));
    fixture.answerRoundOverWifi("critical");
    TEST_ASSERT_EQUAL_UINT(1, fixture.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT(services::AudioService::faultClipLength,
                           fixture.audioAdapter.clips[0].sampleCount);

    // Better, the same, or planned maintenance stays quiet.
    fixture.tick(seconds(60));
    fixture.answerRoundOverWifi("none");
    fixture.tick(seconds(60));
    fixture.answerRoundOverWifi("maintenance");
    TEST_ASSERT_EQUAL_UINT(1, fixture.audioAdapter.clips.size());

    // The last known level survives closing the app and a failed check.
    fixture.service.setActive(false);
    fixture.service.setActive(true);
    for (std::size_t page = 0; page < services::statusSources.size(); ++page) {
        fixture.http.failNetwork();
        fixture.tick();
    }
    fixture.tick(seconds(60));
    fixture.answerRoundOverWifi("major");
    TEST_ASSERT_EQUAL_UINT(2, fixture.audioAdapter.clips.size());
}

// Scenario: unavailable dependency. The app can open only with a route.
void test_the_link_capability_follows_wifi_and_the_companion() {
    ServiceStatusFixture fixture;
    const std::string link = services::serviceStatusLinkCapabilityId;
    fixture.tick();
    TEST_ASSERT_FALSE(fixture.capabilities.isAvailable(link));

    fixture.connectWifi();
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.capabilities.isAvailable(link));
    TEST_ASSERT_TRUE(fixture.wifi.disconnect() == connectivity::WifiDisconnectResult::Disconnected);
    // A lost route is withdrawn only after a grace period.
    fixture.tick(services::ServiceStatusService::linkLossGrace - milliseconds(1));
    TEST_ASSERT_TRUE(fixture.capabilities.isAvailable(link));
    fixture.tick(milliseconds(1));
    TEST_ASSERT_FALSE(fixture.capabilities.isAvailable(link));

    fixture.connectCompanion();
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.capabilities.isAvailable(link));
}

// Review: a brief Wi-Fi or Companion blip must not close the open app.
void test_a_brief_route_loss_keeps_the_link_published() {
    ServiceStatusFixture fixture;
    const std::string link = services::serviceStatusLinkCapabilityId;
    fixture.connectCompanion();
    fixture.tick();
    // A new HELLO restarts the handshake: the session is briefly not ready.
    fixture.transport.incoming.push_back(
        test_support::companionWire(test_support::companionHello("2026-09-30 def5678")));
    fixture.tick(seconds(1));
    TEST_ASSERT_FALSE(fixture.companion.hasLiveCompanion());
    TEST_ASSERT_TRUE(fixture.capabilities.isAvailable(link));
    // The session comes back within the grace period: the timer starts over.
    test_support::completeCompanionHandshake(fixture.transport, fixture.companion);
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.companion.hasLiveCompanion());
    TEST_ASSERT_TRUE(fixture.wifi.connect({"home", "password1"}) ==
                     connectivity::WifiConnectResult::Started);
    fixture.wifiAdapter.adapterState = connectivity::WifiAdapterState::Connected;
    fixture.wifi.update(milliseconds::zero());
    fixture.tick(services::ServiceStatusService::linkLossGrace + seconds(5));
    TEST_ASSERT_TRUE(fixture.capabilities.isAvailable(link));
}

// Review: a client held by an abandoned request must not stall the round.
void test_a_busy_http_client_routes_the_page_through_the_companion() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.connectCompanion();
    fixture.http.holdsAbandoned = true;
    fixture.service.setActive(true);
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.http.urls.empty());
    TEST_ASSERT_EQUAL_UINT(1, fixture.transport.statusRequests().size());
}

void test_a_busy_http_client_without_a_companion_fails_after_the_fetch_timeout() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.http.holdsAbandoned = true;
    fixture.service.setActive(true);
    fixture.tick(services::ServiceStatusService::directFetchTimeout - milliseconds(1));
    TEST_ASSERT_FALSE(fixture.entry(0).checked);
    fixture.tick(milliseconds(1));
    TEST_ASSERT_TRUE(fixture.entry(0).problem == StatusProblem::Unreachable);
}

// Review: a fault cue refused by a busy speaker is played once it is free.
void test_a_fault_cue_waits_for_a_busy_speaker() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.service.setActive(true);
    fixture.answerRoundOverWifi("none");
    fixture.tick(seconds(60));
    fixture.audioAdapter.playing = true;
    fixture.answerRoundOverWifi("major");
    TEST_ASSERT_TRUE(fixture.audioAdapter.clips.empty());
    fixture.audioAdapter.playing = false;
    fixture.tick(milliseconds(100));
    TEST_ASSERT_EQUAL_UINT(1, fixture.audioAdapter.clips.size());
    fixture.tick(milliseconds(100));
    TEST_ASSERT_EQUAL_UINT(1, fixture.audioAdapter.clips.size());
}

// Review: reopening must not stack SERVICE_STATUS requests in the Companion.
void test_reopening_waits_for_the_pending_companion_request() {
    ServiceStatusFixture fixture;
    fixture.connectCompanion();
    for (int open = 0; open < 4; ++open) {
        fixture.service.setActive(true);
        fixture.tick();
        fixture.service.setActive(false);
    }
    fixture.service.setActive(true);
    fixture.tick();
    TEST_ASSERT_EQUAL_UINT(1, fixture.transport.statusRequests().size());
    TEST_ASSERT_TRUE(fixture.companion.hasLiveCompanion());
    // The stale answer frees the slot; it is dropped and the page is asked again.
    fixture.answerCompanion(ServiceStatusLevel::Critical);
    TEST_ASSERT_FALSE(fixture.entry(0).checked);
    TEST_ASSERT_EQUAL_UINT(2, fixture.transport.statusRequests().size());
    fixture.answerCompanion(ServiceStatusLevel::Operational);
    assertLevel(ServiceStatusLevel::Operational, fixture.entry(0).level);
}

// Review: a client that cannot start is not an abandoned request.
void test_an_http_start_failure_falls_back_to_the_companion() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.connectCompanion();
    fixture.http.startFails = true;
    fixture.service.setActive(true);
    fixture.tick();
    TEST_ASSERT_TRUE(fixture.http.urls.empty());
    TEST_ASSERT_EQUAL_UINT(1, fixture.transport.statusRequests().size());
    fixture.answerCompanion(ServiceStatusLevel::Operational);
    TEST_ASSERT_TRUE(fixture.entry(0).route == StatusRoute::Companion);
}

void test_an_http_start_failure_without_a_companion_ends_the_round() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.http.startFails = true;
    fixture.service.setActive(true);
    fixture.tick();
    TEST_ASSERT_FALSE(fixture.service.snapshot().checking);
    for (std::size_t index = 0; index < services::statusSources.size(); ++index)
        TEST_ASSERT_TRUE(fixture.entry(index).problem == StatusProblem::Unreachable);
}

// Review: a direct fetch has an overall deadline.
void test_a_wifi_fetch_that_runs_too_long_is_abandoned() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    fixture.service.setActive(true);
    fixture.tick(services::ServiceStatusService::directFetchTimeout - milliseconds(1));
    TEST_ASSERT_FALSE(fixture.entry(0).checked);
    fixture.tick(milliseconds(1));
    TEST_ASSERT_EQUAL_INT(1, fixture.http.abandoned);
    TEST_ASSERT_TRUE(fixture.entry(0).problem == StatusProblem::Unreachable);
    TEST_ASSERT_EQUAL_UINT(2, fixture.http.urls.size());
}

// Scenario: repeated input.
void test_refresh_starts_a_round_once_and_only_while_open() {
    ServiceStatusFixture fixture;
    fixture.connectWifi();
    const core::Action refresh{services::serviceStatusRefreshActionId, "test", {}};
    TEST_ASSERT_TRUE(fixture.service.handle(refresh) == core::ActionHandlingResult::Rejected);
    fixture.service.setActive(true);
    fixture.answerRoundOverWifi("none");
    TEST_ASSERT_TRUE(fixture.service.handle(refresh) == core::ActionHandlingResult::Handled);
    TEST_ASSERT_TRUE(fixture.service.handle(refresh) == core::ActionHandlingResult::Handled);
    TEST_ASSERT_EQUAL_UINT(5, fixture.http.urls.size());
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_opening_checks_every_page_over_wifi_one_at_a_time);
    RUN_TEST(test_pages_are_checked_again_every_minute_while_open);
    RUN_TEST(test_nothing_is_fetched_while_closed);
    RUN_TEST(test_without_wifi_the_companion_fetches_the_page);
    RUN_TEST(test_a_failed_wifi_fetch_falls_back_to_the_companion);
    RUN_TEST(test_wifi_failure_without_a_companion_marks_the_page_unreachable);
    RUN_TEST(test_without_any_connection_every_page_reports_no_connection);
    RUN_TEST(test_companion_loss_during_a_request_fails_only_that_page);
    RUN_TEST(test_an_answer_after_closing_is_ignored);
    RUN_TEST(test_an_abandoned_wifi_request_delays_the_next_page);
    RUN_TEST(test_a_worse_incident_plays_one_fault_cue_per_round);
    RUN_TEST(test_the_link_capability_follows_wifi_and_the_companion);
    RUN_TEST(test_a_brief_route_loss_keeps_the_link_published);
    RUN_TEST(test_a_busy_http_client_routes_the_page_through_the_companion);
    RUN_TEST(test_a_busy_http_client_without_a_companion_fails_after_the_fetch_timeout);
    RUN_TEST(test_a_fault_cue_waits_for_a_busy_speaker);
    RUN_TEST(test_reopening_waits_for_the_pending_companion_request);
    RUN_TEST(test_an_http_start_failure_falls_back_to_the_companion);
    RUN_TEST(test_an_http_start_failure_without_a_companion_ends_the_round);
    RUN_TEST(test_a_wifi_fetch_that_runs_too_long_is_abandoned);
    RUN_TEST(test_refresh_starts_a_round_once_and_only_while_open);
    return UNITY_END();
}
