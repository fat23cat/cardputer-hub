#pragma once

#include "connectivity/http/http_client.h"
#include "connectivity/wifi/wifi_service.h"
#include "core/actions/action_bus.h"
#include "core/capabilities/capability_registry.h"
#include "services/companion/companion_service.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace cardputer_hub::services {

class AudioService;

inline constexpr char serviceStatusRefreshActionId[] = "service-status.refresh";
// Published while Wi-Fi is connected or a Companion session is ready: a status
// page can be reached one way or the other. SERVICES HEALTH requires it.
inline constexpr char serviceStatusLinkCapabilityId[] = "WIFI_OR_COMPANION";

// A public Atlassian Statuspage, read from its `/api/v2/status.json`.
struct StatusSource {
    const char* name;
    const char* url;
};

inline constexpr std::array<StatusSource, 4> statusSources{{
    {"GITHUB", "https://www.githubstatus.com/api/v2/status.json"},
    {"ANTHROPIC", "https://status.claude.com/api/v2/status.json"},
    {"OPENAI", "https://status.openai.com/api/v2/status.json"},
    {"CURSOR", "https://status.cursor.com/api/v2/status.json"},
}};

// How the last check of a page reached the internet.
enum class StatusRoute : std::uint8_t { None, WiFi, Companion };

enum class StatusProblem : std::uint8_t {
    None,
    // Neither Wi-Fi nor a Companion session was available.
    NoConnection,
    // The page could not be fetched (network, TLS, HTTP error or the Mac failed).
    Unreachable,
    // The page answered but had no known status.
    Unreadable,
};

struct ServiceStatusEntry {
    // Unknown until a check succeeds, and again after a failed check.
    connectivity::ServiceStatusLevel level = connectivity::ServiceStatusLevel::Unknown;
    std::array<char, connectivity::companionMaxStatusDescriptionSize + 1> description{};
    StatusRoute route = StatusRoute::None;
    StatusProblem problem = StatusProblem::None;
    bool checked = false;
    std::chrono::milliseconds sinceCheck{0};
};

struct ServiceStatusSnapshot {
    std::array<ServiceStatusEntry, statusSources.size()> entries{};
    bool checking = false;
    // Time since the last round ended; the next starts at pollInterval. Zero
    // during a round.
    std::chrono::milliseconds sinceRound{0};
    // Changes whenever a level, description, route, problem or `checking`
    // changes; `sinceCheck` and `sinceRound` advance without it.
    std::uint32_t revision = 0;
};

// Checks the status pages while SERVICES HEALTH is open: at once, then every minute,
// one page at a time. A page is fetched over Wi-Fi when it is connected; without
// Wi-Fi, or when the Wi-Fi fetch fails, the Mac Companion fetches it instead.
// A page whose level gets worse than its last known level, to an incident
// (minor or above), plays the fault cue, at most once per round.
class ServiceStatusService final : public core::IActionHandler {
  public:
    static constexpr auto pollInterval = std::chrono::seconds(60);
    // A direct fetch still running after this is abandoned and treated as
    // failed; so is a page that waited this long for a client still held by an
    // abandoned request.
    static constexpr auto directFetchTimeout = std::chrono::seconds(15);
    // WIFI_OR_COMPANION is withdrawn only after both routes have been gone
    // this long, so a Wi-Fi retry or a Companion re-handshake does not close
    // an open app. A new route publishes it at once.
    static constexpr auto linkLossGrace = std::chrono::seconds(20);
    // A fault cue refused because the speaker is busy is retried this long.
    static constexpr auto faultRetryWindow = std::chrono::seconds(2);

    ServiceStatusService(connectivity::IHttpClient& http, const connectivity::WiFiService& wifi,
                         CompanionService& companion, core::CapabilityRegistry& capabilities,
                         AudioService* audio = nullptr)
        : http_(http), wifi_(wifi), companion_(companion), capabilities_(capabilities),
          audio_(audio) {}

    void setActive(bool active);
    bool active() const noexcept { return active_; }
    void update(std::chrono::milliseconds elapsed);
    const ServiceStatusSnapshot& snapshot() const noexcept { return snapshot_; }
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    enum class Fetch : std::uint8_t { None, WiFi, Companion };

    void startRound();
    void advance();
    void startCurrent(bool allowWiFi);
    void finishDirect(std::chrono::milliseconds elapsed);
    void fallBackFromWiFi();
    void finishCompanion();
    void record(const connectivity::CompanionServiceStatus& status, StatusRoute route);
    void fail(StatusProblem problem, StatusRoute route);
    void endRound();
    bool companionReady() const noexcept { return companion_.hasLiveCompanion(); }
    void publishLink(std::chrono::milliseconds elapsed);
    void playPendingFault(std::chrono::milliseconds elapsed);

    connectivity::IHttpClient& http_;
    const connectivity::WiFiService& wifi_;
    CompanionService& companion_;
    core::CapabilityRegistry& capabilities_;
    AudioService* audio_ = nullptr;
    bool linkPublished_ = false;
    std::chrono::milliseconds linkLostElapsed_{0};
    bool faultPending_ = false;
    std::chrono::milliseconds faultWait_{0};
    ServiceStatusSnapshot snapshot_{};
    // The last successful level of each page in this boot, for the fault cue.
    std::array<connectivity::ServiceStatusLevel, statusSources.size()> lastKnown_{};
    bool active_ = false;
    bool roundActive_ = false;
    bool alerted_ = false;
    // The current page already failed over Wi-Fi in this round.
    bool wifiFailed_ = false;
    std::size_t current_ = 0;
    Fetch fetch_ = Fetch::None;
    std::uint8_t requestId_ = 0;
    std::uint16_t session_ = 0;
    std::chrono::milliseconds fetchElapsed_{0};
    // Time the current page has waited without a request in flight.
    std::chrono::milliseconds waitElapsed_{0};
};

} // namespace cardputer_hub::services
