#pragma once
#include "apps/hosts/host_settings.h"
#include "apps/launcher/launcher.h"
#include "apps/network/wifi_settings.h"
#include "core/navigation/navigation_stack.h"
#include "services/audio/audio_service.h"
#include "services/network/network_service.h"

namespace cardputer_hub::apps {
class MiniAppRuntime;

class ApplicationShell final : public core::IActionHandler {
  public:
    ApplicationShell(services::HostService& hosts, services::NetworkService& network,
                     core::ActionBus& actions, core::IDisplayAdapter& display,
                     HostSettings& settings, WiFiSettings& wifiSettings,
                     services::AudioService& audio, MiniAppRuntime& miniApps);
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed = {},
                std::optional<std::uint8_t> batteryPercent = std::nullopt);
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    struct HomeConnectionFrame {
        std::optional<std::uint32_t> activeHost;
        std::string hostName;
        services::HostConnectionStatus status = services::HostConnectionStatus::Off;
    };

    struct HomeNetworkFrame {
        bool configured = false;
        bool enabled = false;
        services::WifiConnectionStatus connection = services::WifiConnectionStatus::Off;
    };

    struct SettingsFrame {
        std::uint8_t selection = 0;
        std::uint8_t volume = 0;
    };

    void ensureLauncher();
    void restoreLauncherFromMiniApp(bool showUnavailableReason);
    void finishMiniAppUpdate(bool missingCapability);
    void applyMiniAppUpdate(const core::InputEvents& input, std::chrono::milliseconds elapsed);
    void playInputFeedback(const core::InputEvent& event);
    void routeMiniAppEvent(const core::InputEvent& event);
    void routeSystemEvent(const core::InputEvent& event);
    void tickCurrentPresentation(std::chrono::milliseconds elapsed,
                                 std::optional<std::uint8_t> batteryPercent);
    void renderHome(std::chrono::milliseconds elapsed, std::optional<std::uint8_t> batteryPercent);
    void renderSettings();
    bool atSettings() const;
    bool atBluetooth() const;
    bool atWifi() const;
    bool atLauncher() const;
    bool atHome() const;
    services::HostService& hosts_;
    services::NetworkService& network_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    HostSettings& settings_;
    WiFiSettings& wifiSettings_;
    services::AudioService& audio_;
    MiniAppRuntime& miniApps_;
    Launcher launcher_;
    core::NavigationStack navigation_;
    std::optional<HomeConnectionFrame> homeConnectionFrame_;
    std::optional<HomeNetworkFrame> homeNetworkFrame_;
    std::optional<std::uint8_t> homeBatteryPercent_;
    unsigned homePhaseMilliseconds_ = 0;
    std::optional<SettingsFrame> settingsFrame_;
    std::uint8_t settingsSelection_ = 0;
    bool showingMiniApp_ = false;
    bool miniAppUpdateInProgress_ = false;
    bool miniAppCloseRequested_ = false;
};
} // namespace cardputer_hub::apps
