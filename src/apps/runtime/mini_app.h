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

    // Offered the plain Escape key before the shell closes the app. Return true
    // when the app used it for its own back navigation, for example to leave a
    // detail view; the app then stays open. Returning false lets the shell close
    // the app and return to Launcher.
    virtual bool handleBack() { return false; }
};

} // namespace cardputer_hub::apps
