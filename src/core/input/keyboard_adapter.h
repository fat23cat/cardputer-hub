#pragma once

#include "core/input/input_event.h"

namespace cardputer_hub::core {

class IKeyboardAdapter {
  public:
    virtual ~IKeyboardAdapter() = default;
    virtual void poll(InputEvents& events) = 0;
};

} // namespace cardputer_hub::core
