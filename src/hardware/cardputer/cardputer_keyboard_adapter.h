#pragma once

#include "core/input/keyboard_adapter.h"
#include "core/input/keyboard_event_translator.h"

namespace cardputer_hub::hardware {

class CardputerKeyboardAdapter final : public core::IKeyboardAdapter {
  public:
    void poll(core::InputEvents& events) override;

  private:
    core::KeyboardEventTranslator translator_;
};

} // namespace cardputer_hub::hardware
