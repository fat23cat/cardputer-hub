#pragma once
#include "apps/hosts/host_settings.h"
#include "core/navigation/navigation_stack.h"

namespace cardputer_hub::apps {
class ApplicationShell final : public core::IActionHandler {
  public:
    ApplicationShell(services::HostService& hosts, core::ActionBus& actions,
                     core::IDisplayAdapter& display, HostSettings& settings);
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed = {},
                std::optional<std::uint8_t> batteryPercent = std::nullopt);
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    void renderHome(std::chrono::milliseconds elapsed, std::optional<std::uint8_t> batteryPercent);
    void renderSettings();
    bool atSettings() const;
    bool atBluetooth() const;
    bool atHome() const;
    services::HostService& hosts_;
    core::ActionBus& actions_;
    core::IDisplayAdapter& display_;
    HostSettings& settings_;
    core::NavigationStack navigation_;
    std::string homeFrame_;
    std::string batteryFrame_;
    unsigned homePhaseMilliseconds_ = 0;
    bool settingsFrame_ = false;
};
} // namespace cardputer_hub::apps
