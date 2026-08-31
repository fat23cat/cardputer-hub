#pragma once

#include "core/display/display_adapter.h"
#include "core/input/keyboard_adapter.h"
#include "core/lifecycle/build_info.h"
#include "core/logging/logger.h"
#include "core/platform/platform_adapter.h"

namespace cardputer_hub::core {

class SystemRuntime {
  public:
    SystemRuntime(IPlatformAdapter& platform, IKeyboardAdapter& keyboard, IDisplayAdapter& display,
                  Logger& logger, const BuildInfo& buildInfo) noexcept;

    void start();
    const InputEvents& update();

  private:
    IPlatformAdapter& platform_;
    IKeyboardAdapter& keyboard_;
    IDisplayAdapter& display_;
    Logger& logger_;
    const BuildInfo& buildInfo_;
    InputEvents inputEvents_;
    bool started_ = false;
};

} // namespace cardputer_hub::core
