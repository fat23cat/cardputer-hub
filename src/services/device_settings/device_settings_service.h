#pragma once

#include "core/actions/action_bus.h"
#include "core/power/display_power_controller.h"
#include "services/configuration/configuration_service.h"
#include "services/indicator/indicator_service.h"

namespace cardputer_hub::services {

class DeviceSettingsService final : public core::IActionHandler {
  public:
    DeviceSettingsService(ConfigurationService& configuration,
                          core::DisplayPowerController& display,
                          IndicatorService& indicator) noexcept
        : configuration_(configuration), display_(display), indicator_(indicator) {}

    bool start();
    core::ScreenTimeoutMode screenTimeout() const noexcept {
        return configuration_.value().device.screenTimeout;
    }
    std::uint8_t screenBrightness() const noexcept {
        return configuration_.value().device.screenBrightness;
    }
    std::uint8_t ledBrightness() const noexcept {
        return configuration_.value().device.ledBrightness;
    }
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    ConfigurationService& configuration_;
    core::DisplayPowerController& display_;
    IndicatorService& indicator_;
};

} // namespace cardputer_hub::services
