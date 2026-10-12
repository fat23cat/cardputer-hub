#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "connectivity/http/http_server.h"
#include "connectivity/wifi/wifi_access_point.h"
#include "core/actions/action_bus.h"
#include "core/logging/logger.h"
#include "core/platform/random_source.h"
#include "core/storage/storage.h"
#include "services/web_access/web_app.h"
#include "services/web_access/web_json.h"

namespace cardputer_hub::services {

inline constexpr char webAccessSetEnabledActionId[] = "web.set-enabled";
inline constexpr char webAccessLogin[] = "admin";
// The ESP-IDF default access-point address.
inline constexpr char webAccessAddress[] = "192.168.4.1";
inline constexpr char webSessionCookieName[] = "hub_session";
inline constexpr std::size_t webAccessPasswordLength = 12;

enum class WebAccessState : std::uint8_t {
    Off,
    Running,
    // Starting failed or the server stopped by itself; nothing is running.
    Error,
};

struct WebAccessStatus {
    WebAccessState state = WebAccessState::Off;
    // While Running: what the Cardputer shows so a browser can join.
    std::string ssid;
    std::string password;
    std::size_t clients = 0;
};

enum class WebAccessResult : std::uint8_t {
    Success,
    AccessPointError,
    ServerError,
};

// The local web UI: a WPA2 access point, an HTTP server on it, sign-in and the
// routing to the registered web apps. Off at boot; `web.set-enabled` turns it
// on or off. The network name and password are generated once and kept in
// storage. update() is called once per main loop and answers at most one
// request; nothing here runs on the server's task.
class WebAccessService final : public core::IActionHandler {
  public:
    static constexpr std::size_t maximumApps = 8;
    static constexpr std::size_t maximumSessions = 4;
    static constexpr std::chrono::milliseconds sessionIdleTimeout = std::chrono::minutes(30);
    static constexpr std::size_t signInAttempts = 5;
    static constexpr std::chrono::milliseconds signInLockout = std::chrono::seconds(30);
    static constexpr std::chrono::milliseconds clientPollInterval{500};

    WebAccessService(connectivity::IWifiAccessPointAdapter& accessPoint,
                     connectivity::IHttpServer& server, core::Storage& storage,
                     core::IRandomSource& random, core::Logger* logger = nullptr) noexcept
        : accessPoint_(accessPoint), server_(server), storage_(storage), random_(random),
          logger_(logger) {}

    // Registration order is the order of the tiles and tabs.
    bool addApp(IWebApp& app);
    WebAccessResult setEnabled(bool enabled);
    void update(std::chrono::milliseconds elapsed);
    [[nodiscard]] const WebAccessStatus& status() const noexcept { return status_; }
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    struct Session {
        std::string token;
        std::chrono::milliseconds idle{0};
    };
    struct Credentials {
        std::string ssid;
        std::string password;
    };

    Credentials loadCredentials();
    void stopAll();
    connectivity::HttpResponse route(const connectivity::HttpRequest& request);
    // The page, its icons and manifest; served without a session.
    connectivity::HttpResponse staticFile(std::string_view path) const;
    connectivity::HttpResponse signIn(const connectivity::HttpRequest& request);
    connectivity::HttpResponse signOut(const connectivity::HttpRequest& request);
    connectivity::HttpResponse hub();
    Session* session(const connectivity::HttpRequest& request);
    std::string newToken();
    void log(core::LogLevel level, const char* message) const;

    connectivity::IWifiAccessPointAdapter& accessPoint_;
    connectivity::IHttpServer& server_;
    core::Storage& storage_;
    core::IRandomSource& random_;
    core::Logger* logger_;
    std::vector<IWebApp*> apps_;
    // Kept once loaded or generated, so a password storage could not keep
    // still stays the same until the next restart.
    std::optional<Credentials> credentials_;
    WebAccessStatus status_;
    std::vector<Session> sessions_;
    std::size_t failedSignIns_ = 0;
    std::chrono::milliseconds lockout_{0};
    std::chrono::milliseconds sinceClientPoll_{0};
};

} // namespace cardputer_hub::services
