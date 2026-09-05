#pragma once

#include <memory>

#include "core/input/keyboard_adapter.h"
#include "core/input/keyboard_event_translator.h"
#include "hardware/cardputer/cardputer_adv_keyboard_layout.h"

class Adafruit_TCA8418;

namespace cardputer_hub::hardware {

class CardputerKeyboardAdapter final : public core::IKeyboardAdapter {
  public:
    CardputerKeyboardAdapter();
    ~CardputerKeyboardAdapter() override;

    void poll(core::InputEvents& events) override;

  private:
    bool initialize();

    std::unique_ptr<Adafruit_TCA8418> controller_;
    CardputerAdvPressedKeys pressedKeys_{};
    core::KeyboardEventTranslator translator_;
    std::uint64_t retryAfterMilliseconds_ = 0;
    bool initialized_ = false;
};

} // namespace cardputer_hub::hardware
