#pragma once
#include "apps/hosts/host_settings.h"
#include "core/navigation/navigation_stack.h"
#include "services/audio/audio_service.h"

namespace cardputer_hub::apps {
class ApplicationShell final : public core::IActionHandler {
  public:
    ApplicationShell(services::HostService& hosts, core::ActionBus& actions,
                     core::IDisplayAdapter& display, HostSettings& settings,
                     services::AudioService& audio);
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed = {},
                std::optional<std::uint8_t> batteryPercent = std::nullopt);
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    enum class HomeConnectionStatus : std::uint8_t {
        Connecting,
        Error,
        Off,
        Ready,
        Pairing,
        Securing,
    };

    struct HomeConnectionFrame {
        std::optional<std::uint32_t> activeHost;
        std::string hostName;
        HomeConnectionStatus status = HomeConnectionStatus::Off;
    };

    struct SettingsFrame {
        std::uint8_t selection = 0;
        std::uint8_t volume = 0;
    };

    void renderHome(std::chrono::milliseconds elapsed, std::optional<std::uint8_t> batteryPercent);
    void renderSettings();
    bool atSettings() const;
    bool atBluetooth() const;
    bool atHome() const;
    services::HostService& hosts_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    HostSettings& settings_;
    services::AudioService& audio_;
    core::NavigationStack navigation_;
    std::optional<HomeConnectionFrame> homeConnectionFrame_;
    std::optional<std::uint8_t> homeBatteryPercent_;
    unsigned homePhaseMilliseconds_ = 0;
    std::optional<SettingsFrame> settingsFrame_;
    std::uint8_t settingsSelection_ = 0;
};
} // namespace cardputer_hub::apps
