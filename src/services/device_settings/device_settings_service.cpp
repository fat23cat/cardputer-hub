#include "services/device_settings/device_settings_service.h"

#include <variant>

namespace cardputer_hub::services {

bool DeviceSettingsService::start() {
    if (configuration_.ensureLoaded() != ConfigurationResult::Success)
        return false;
    display_.setBrightnessPercent(screenBrightness());
    display_.setTimeoutMode(screenTimeout());
    indicator_.setMaximumBrightnessPercent(ledBrightness());
    return true;
}

core::ActionHandlingResult DeviceSettingsService::handle(const core::Action& action) {
    const auto* value = action.findParameter("delta");
    const auto* delta = value ? std::get_if<std::int32_t>(value) : nullptr;
    if (!delta || configuration_.ensureLoaded() != ConfigurationResult::Success)
        return core::ActionHandlingResult::Rejected;
    auto next = configuration_.value();
    if (action.id == "display.timeout.step" && (*delta == -1 || *delta == 1)) {
        const auto requested = static_cast<std::int32_t>(screenTimeout()) + *delta;
        if (requested < 0 || requested > 2)
            return core::ActionHandlingResult::Rejected;
        next.device.screenTimeout = static_cast<core::ScreenTimeoutMode>(requested);
    } else if (action.id == "display.brightness.step" && (*delta == -10 || *delta == 10)) {
        const auto requested = static_cast<std::int32_t>(screenBrightness()) + *delta;
        if (requested < 20 || requested > 100)
            return core::ActionHandlingResult::Rejected;
        next.device.screenBrightness = static_cast<std::uint8_t>(requested);
    } else if (action.id == "indicator.brightness.step" && (*delta == -1 || *delta == 1)) {
        const auto requested = static_cast<std::int32_t>(ledBrightness()) + *delta;
        if (requested < 1 || requested > 10)
            return core::ActionHandlingResult::Rejected;
        next.device.ledBrightness = static_cast<std::uint8_t>(requested);
    } else {
        return core::ActionHandlingResult::Rejected;
    }
    if (configuration_.save(next) != ConfigurationResult::Success)
        return core::ActionHandlingResult::Rejected;
    if (action.id == "display.timeout.step")
        display_.setTimeoutMode(screenTimeout());
    else if (action.id == "display.brightness.step")
        display_.setBrightnessPercent(screenBrightness());
    else
        indicator_.setMaximumBrightnessPercent(ledBrightness());
    return core::ActionHandlingResult::Handled;
}

} // namespace cardputer_hub::services
