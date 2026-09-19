#pragma once

#include "core/input/input_event.h"

namespace cardputer_hub::core {

struct KeyboardPollResult {
    // Any new press edge, including a modifier such as Fn that produces no
    // InputEvent. Releases and held keys are not new activity.
    bool physicalPress = false;
};

class IKeyboardAdapter {
  public:
    virtual ~IKeyboardAdapter() = default;
    virtual KeyboardPollResult poll(InputEvents& events) = 0;
};

} // namespace cardputer_hub::core
