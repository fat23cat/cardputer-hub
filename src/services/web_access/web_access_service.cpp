#include "services/web_access/web_access_service.h"

#include <algorithm>
#include <utility>

#include "services/web_access/web_ui_asset.h"

namespace cardputer_hub::services {
namespace {

constexpr const char* component = "WebAccessService";
constexpr std::uint8_t credentialsFormat = 1;
// No l, o, 0 or 1: the password is read off the Cardputer screen and typed.
constexpr char passwordAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
static_assert(sizeof(passwordAlphabet) - 1 == 32, "an unbiased five-bit alphabet");
constexpr char hexDigits[] = "0123456789abcdef";
constexpr char upperHexDigits[] = "0123456789ABCDEF";
constexpr std::size_t tokenBytes = 16;

core::StorageAddress credentialsAddress() { return {"web", "access"}; }

core::StorageBytes encode(const std::string& ssid, const std::string& password) {
    core::StorageBytes bytes{credentialsFormat, static_cast<std::uint8_t>(ssid.size())};
    bytes.insert(bytes.end(), ssid.begin(), ssid.end());
    bytes.push_back(static_cast<std::uint8_t>(password.size()));
    bytes.insert(bytes.end(), password.begin(), password.end());
    return bytes;
}

std::optional<connectivity::WifiAccessPointConfig> decode(const core::StorageBytes& bytes) {
    std::size_t position = 0;
    const auto field = [&](std::string& value) {
        if (position >= bytes.size())
            return false;
        const std::size_t size = bytes[position++];
        if (bytes.size() - position < size)
            return false;
        value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(position),
                     bytes.begin() + static_cast<std::ptrdiff_t>(position + size));
        position += size;
        return true;
    };
    connectivity::WifiAccessPointConfig config;
    if (bytes.empty() || bytes[position++] != credentialsFormat || !field(config.ssid) ||
        !field(config.passphrase) || position != bytes.size() ||
        !connectivity::validWifiAccessPointConfig(config))
        return std::nullopt;
    return config;
}

// Compares equal-length secrets without stopping at the first difference.
bool equalSecret(std::string_view left, std::string_view right) {
    if (left.size() != right.size() || right.empty())
        return false;
    unsigned char difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index)
        difference |= static_cast<unsigned char>(left[index] ^ right[index]);
    return difference == 0;
}

void securityHeaders(connectivity::HttpResponse& response) {
    response.headers.push_back({"X-Content-Type-Options", "nosniff"});
    response.headers.push_back({"Referrer-Policy", "no-referrer"});
}

} // namespace

bool WebAccessService::addApp(IWebApp& app) {
    if (apps_.size() == maximumApps || app.id().empty() ||
        std::any_of(apps_.begin(), apps_.end(),
                    [&app](const IWebApp* known) { return known->id() == app.id(); }))
        return false;
    for (const char character : app.id()) {
        if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
              character == '-'))
            return false;
    }
    apps_.push_back(&app);
    return true;
}

WebAccessService::Credentials WebAccessService::loadCredentials() {
    if (credentials_)
        return *credentials_;
    const auto stored = storage_.read(credentialsAddress());
    if (stored.status == core::StorageReadStatus::Found) {
        if (const auto config = decode(stored.data))
            return *(credentials_ = Credentials{config->ssid, config->passphrase});
    }
    std::array<std::uint8_t, 2 + webAccessPasswordLength> random{};
    random_.fill(random.data(), random.size());
    Credentials credentials;
    credentials.ssid = "HUB-";
    for (std::size_t index = 0; index < 2; ++index) {
        credentials.ssid.push_back(upperHexDigits[random[index] >> 4]);
        credentials.ssid.push_back(upperHexDigits[random[index] & 0x0f]);
    }
    for (std::size_t index = 0; index < webAccessPasswordLength; ++index)
        credentials.password.push_back(passwordAlphabet[random[2 + index] & 0x1f]);
    if (storage_.write(credentialsAddress(), encode(credentials.ssid, credentials.password)) !=
        core::StorageWriteStatus::Stored)
        log(core::LogLevel::Warning, "access point credentials not stored");
    credentials_ = credentials;
    return credentials;
}

WebAccessResult WebAccessService::setEnabled(bool enabled) {
    if (!enabled) {
        if (status_.state == WebAccessState::Running)
            log(core::LogLevel::Info, "stopped");
        stopAll();
        status_.state = WebAccessState::Off;
        return WebAccessResult::Success;
    }
    if (status_.state == WebAccessState::Running)
        return WebAccessResult::Success;

    const auto credentials = loadCredentials();
    if (accessPoint_.startAccessPoint({credentials.ssid, credentials.password}) !=
        connectivity::WifiAdapterResult::Success) {
        log(core::LogLevel::Error, "access point did not start");
        stopAll();
        status_.state = WebAccessState::Error;
        return WebAccessResult::AccessPointError;
    }
    if (!server_.start()) {
        log(core::LogLevel::Error, "server did not start");
        stopAll();
        status_.state = WebAccessState::Error;
        return WebAccessResult::ServerError;
    }
    status_ = {WebAccessState::Running, credentials.ssid, credentials.password,
               accessPoint_.accessPointClientCount()};
    sinceClientPoll_ = {};
    log(core::LogLevel::Info, "running");
    return WebAccessResult::Success;
}

void WebAccessService::stopAll() {
    server_.stop();
    (void)accessPoint_.stopAccessPoint();
    for (auto* app : apps_)
        app->reset();
    sessions_.clear();
    failedSignIns_ = 0;
    lockout_ = {};
    status_ = {};
}

void WebAccessService::update(std::chrono::milliseconds elapsed) {
    if (status_.state != WebAccessState::Running)
        return;
    if (!server_.running()) {
        log(core::LogLevel::Error, "server stopped");
        stopAll();
        status_.state = WebAccessState::Error;
        return;
    }
    for (auto& session : sessions_)
        session.idle += elapsed;
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [](const Session& session) { return session.idle >= sessionIdleTimeout; }),
        sessions_.end());
    lockout_ = elapsed >= lockout_ ? std::chrono::milliseconds(0) : lockout_ - elapsed;
    sinceClientPoll_ += elapsed;
    if (sinceClientPoll_ >= clientPollInterval) {
        sinceClientPoll_ = {};
        status_.clients = accessPoint_.accessPointClientCount();
    }
    if (auto request = server_.poll()) {
        auto response = route(*request);
        securityHeaders(response);
        server_.respond(request->id, std::move(response));
    }
}

connectivity::HttpResponse WebAccessService::route(const connectivity::HttpRequest& request) {
    constexpr std::string_view apiPrefix = "/api/";
    const std::string_view path = request.path;
    if (path.substr(0, apiPrefix.size()) != apiPrefix) {
        if (request.method == connectivity::HttpMethod::Get)
            return staticFile(path);
        return staticFile({});
    }
    // A cross-site form or image cannot set this header; a cross-site script
    // would need a preflight the server never grants.
    if (request.method != connectivity::HttpMethod::Get && request.requestMarker != "1")
        return webErrorResponse(403, "request-marker");

    const auto rest = path.substr(apiPrefix.size());
    if (rest == "session") {
        if (request.method == connectivity::HttpMethod::Post)
            return signIn(request);
        if (request.method == connectivity::HttpMethod::Delete)
            return signOut(request);
        return webErrorResponse(405, "method");
    }
    if (session(request) == nullptr)
        return webErrorResponse(401, "session");
    if (rest == "hub")
        return request.method == connectivity::HttpMethod::Get ? hub()
                                                               : webErrorResponse(405, "method");
    const auto slash = rest.find('/');
    const auto appId = rest.substr(0, slash);
    for (auto* app : apps_) {
        if (app->id() == appId)
            return app->handle(request, slash == std::string_view::npos ? std::string_view()
                                                                        : rest.substr(slash));
    }
    return webErrorResponse(404, "not-found");
}

connectivity::HttpResponse WebAccessService::staticFile(std::string_view path) const {
    connectivity::HttpResponse response;
    const auto* file = webUiFile(path);
    if (file == nullptr) {
        response.status = 404;
        response.contentType = "text/plain; charset=utf-8";
        response.body = "Not found";
        return response;
    }
    response.contentType = std::string(file->contentType);
    response.staticBody = file->body;
    if (file->gzip)
        response.headers.push_back({"Content-Encoding", "gzip"});
    if (path == "/") {
        response.headers.push_back({"Cache-Control", "no-cache"});
        response.headers.push_back(
            {"Content-Security-Policy",
             "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
             "connect-src 'self'; img-src 'self' data:; manifest-src 'self'; base-uri 'none'; "
             "form-action 'self'; frame-ancestors 'none'"});
    } else {
        // Icons and the manifest change only with the firmware.
        response.headers.push_back({"Cache-Control", "public, max-age=86400"});
    }
    return response;
}

connectivity::HttpResponse WebAccessService::signIn(const connectivity::HttpRequest& request) {
    if (lockout_.count() > 0)
        return webErrorResponse(429, "locked");
    const auto fields = parseWebJsonStringObject(request.body, 2);
    if (!fields || fields->count("login") == 0 || fields->count("password") == 0)
        return webErrorResponse(400, "malformed");
    const bool loginMatches = equalSecret(fields->at("login"), webAccessLogin);
    const bool passwordMatches = equalSecret(fields->at("password"), status_.password);
    if (!loginMatches || !passwordMatches) {
        if (++failedSignIns_ >= signInAttempts) {
            failedSignIns_ = 0;
            lockout_ = signInLockout;
            log(core::LogLevel::Warning, "sign-in locked");
        }
        return webErrorResponse(401, "credentials");
    }
    failedSignIns_ = 0;
    if (sessions_.size() == maximumSessions) {
        sessions_.erase(std::max_element(
            sessions_.begin(), sessions_.end(),
            [](const Session& left, const Session& right) { return left.idle < right.idle; }));
    }
    sessions_.push_back({newToken(), {}});
    auto response = webEmptyResponse(204);
    response.headers.push_back({"Set-Cookie", std::string(webSessionCookieName) + "=" +
                                                  sessions_.back().token +
                                                  "; Path=/; HttpOnly; SameSite=Strict"});
    log(core::LogLevel::Info, "signed in");
    return response;
}

connectivity::HttpResponse WebAccessService::signOut(const connectivity::HttpRequest& request) {
    if (auto* current = session(request)) {
        const auto token = current->token;
        sessions_.erase(
            std::remove_if(sessions_.begin(), sessions_.end(),
                           [&token](const Session& known) { return known.token == token; }),
            sessions_.end());
    }
    auto response = webEmptyResponse(204);
    response.headers.push_back(
        {"Set-Cookie",
         std::string(webSessionCookieName) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict"});
    return response;
}

connectivity::HttpResponse WebAccessService::hub() {
    std::string body = R"({"device":{"network":)";
    appendWebJsonString(body, status_.ssid);
    body += R"(,"clients":)" + std::to_string(status_.clients) + R"(,"address":)";
    appendWebJsonString(body, webAccessAddress);
    body += R"(},"apps":[)";
    for (std::size_t index = 0; index < apps_.size(); ++index) {
        if (index != 0)
            body.push_back(',');
        body += R"({"id":)";
        appendWebJsonString(body, apps_[index]->id());
        body += R"(,"title":)";
        appendWebJsonString(body, apps_[index]->title());
        body += R"(,"summary":)" + apps_[index]->summaryJson() + "}";
    }
    body += "]}";
    return webJsonResponse(200, std::move(body));
}

WebAccessService::Session* WebAccessService::session(const connectivity::HttpRequest& request) {
    const auto token = connectivity::httpCookieValue(request.cookie, webSessionCookieName);
    if (!token || token->size() != tokenBytes * 2)
        return nullptr;
    for (auto& known : sessions_) {
        if (equalSecret(known.token, *token)) {
            known.idle = {};
            return &known;
        }
    }
    return nullptr;
}

std::string WebAccessService::newToken() {
    std::array<std::uint8_t, tokenBytes> bytes{};
    random_.fill(bytes.data(), bytes.size());
    std::string token;
    for (const auto byte : bytes) {
        token.push_back(hexDigits[byte >> 4]);
        token.push_back(hexDigits[byte & 0x0f]);
    }
    return token;
}

core::ActionHandlingResult WebAccessService::handle(const core::Action& action) {
    if (action.id != webAccessSetEnabledActionId)
        return core::ActionHandlingResult::Rejected;
    const auto* value = action.findParameter("enabled");
    const auto* enabled = value ? std::get_if<bool>(value) : nullptr;
    if (!enabled)
        return core::ActionHandlingResult::Rejected;
    (void)setEnabled(*enabled);
    return core::ActionHandlingResult::Handled;
}

void WebAccessService::log(core::LogLevel level, const char* message) const {
    if (logger_)
        logger_->log({level, component, message});
}

} // namespace cardputer_hub::services
