#pragma once
#include "apps/common/focus_plate_motion.h"
#include "apps/hosts/host_settings.h"
#include "apps/launcher/launcher.h"
#include "apps/network/wifi_settings.h"
#include "core/capabilities/capability_registry.h"
#include "core/navigation/navigation_stack.h"
#include "services/audio/audio_service.h"
#include "services/device_settings/device_settings_service.h"
#include "services/network/network_service.h"

namespace cardputer_hub::apps {
class MiniAppRuntime;

class ApplicationShell final : public core::IActionHandler {
  public:
    ApplicationShell(services::HostService& hosts, services::NetworkService& network,
                     core::ActionBus& actions, core::IDisplayAdapter& display,
                     HostSettings& settings, WiFiSettings& wifiSettings,
                     services::AudioService& audio, services::DeviceSettingsService& deviceSettings,
                     MiniAppRuntime& miniApps, core::CapabilityRegistry& capabilities);
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed = {},
                std::optional<std::uint8_t> batteryPercent = std::nullopt, bool displayOff = false);
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    struct HomeStatusFrame {
        std::uint8_t wifi = 0;
        std::uint8_t bluetooth = 0;
        std::optional<std::uint8_t> batteryPercent;
        std::string connectedDeviceName;
    };

    struct SettingsFrame {
        std::uint8_t selection = 0;
        std::uint8_t volume = 0;
        core::ScreenTimeoutMode timeout = core::ScreenTimeoutMode::Normal;
        std::uint8_t screenBrightness = 100;
        std::uint8_t ledBrightness = 3;
    };

    void ensureLauncher();
    void restoreLauncherFromMiniApp(bool showUnavailableReason);
    void finishMiniAppUpdate(bool missingCapability);
    void applyMiniAppUpdate(const core::InputEvents& input, std::chrono::milliseconds elapsed);
    void playInputFeedback(const core::InputEvent& event);
    void routeMiniAppEvent(const core::InputEvent& event);
    void routeSystemEvent(const core::InputEvent& event);
    void tickCurrentPresentation(std::chrono::milliseconds elapsed,
                                 std::optional<std::uint8_t> batteryPercent, bool displayOff,
                                 bool transitionWasActive);
    void renderHome(std::chrono::milliseconds elapsed, std::optional<std::uint8_t> batteryPercent,
                    bool displayOff, bool transitionPaused);
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
    services::DeviceSettingsService& deviceSettings_;
    MiniAppRuntime& miniApps_;
    core::CapabilityRegistry& capabilities_;
    Launcher launcher_;
    core::NavigationStack navigation_;
    std::optional<HomeStatusFrame> homeStatusFrame_;
    bool homeSettingsFocused_ = false;
    std::optional<bool> homeRenderedSettingsFocused_;
    std::optional<int> homeRenderedPlateX_;
    FocusPlateMotion homePlateMotion_;
    std::uint64_t homePhaseMilliseconds_ = 0;
    std::uint64_t homeFrameAccumulatorMilliseconds_ = 0;
    bool homeAmbientRendered_ = false;
    std::optional<SettingsFrame> settingsFrame_;
    std::uint8_t settingsSelection_ = 0;
    bool showingMiniApp_ = false;
    bool miniAppUpdateInProgress_ = false;
    bool miniAppCloseRequested_ = false;
};
} // namespace cardputer_hub::apps
