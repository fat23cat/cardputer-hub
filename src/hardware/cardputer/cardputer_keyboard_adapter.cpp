#include "hardware/cardputer/cardputer_keyboard_adapter.h"

#include <Adafruit_TCA8418.h>

#include <cstdint>
#include <memory>

#include "esp_timer.h"

namespace cardputer_hub::hardware {
namespace {

constexpr std::uint64_t retryIntervalMilliseconds = 1000;

std::uint64_t millisecondsSinceBoot() {
    return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U;
}

} // namespace

CardputerKeyboardAdapter::CardputerKeyboardAdapter() = default;
CardputerKeyboardAdapter::~CardputerKeyboardAdapter() = default;

bool CardputerKeyboardAdapter::initialize() {
    controller_ = std::make_unique<Adafruit_TCA8418>();
    if (!controller_->begin() || !controller_->matrix(7, 8)) {
        controller_.reset();
        return false;
    }

    controller_->flush();
    pressedKeys_.fill(false);
    initialized_ = true;
    return true;
}

void CardputerKeyboardAdapter::poll(core::InputEvents& events) {
    if (!initialized_) {
        const auto now = millisecondsSinceBoot();
        if (now < retryAfterMilliseconds_) {
            events.clear();
            return;
        }
        if (!initialize()) {
            retryAfterMilliseconds_ = now + retryIntervalMilliseconds;
            events.clear();
            return;
        }
    }

    while (controller_->available() > 0) {
        const auto edge = decodeCardputerAdvKeyEvent(controller_->getEvent());
        if (!edge.has_value()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(edge->row) * cardputerAdvKeyboardColumns +
                           static_cast<std::size_t>(edge->column);
        pressedKeys_[index] = edge->pressed;
    }

    translator_.translate(cardputerAdvKeyboardSnapshot(pressedKeys_), events);
}

} // namespace cardputer_hub::hardware
