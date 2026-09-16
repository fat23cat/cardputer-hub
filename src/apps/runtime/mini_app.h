#pragma once

#include "core/input/input_event.h"

#include <chrono>

namespace cardputer_hub::apps {

class IMiniApp {
  public:
    virtual ~IMiniApp() = default;

    virtual void onActivate() = 0;
    virtual void onDeactivate() = 0;

    virtual void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) = 0;
};

} // namespace cardputer_hub::apps
