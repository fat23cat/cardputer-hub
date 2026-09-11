#pragma once

#include <chrono>

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
    const InputEvents& update(std::chrono::milliseconds elapsed = {});
    bool splashFinished() const noexcept { return splashElapsed_ >= splashDuration; }

  private:
    void drawSplash();
    void advanceSplash(std::chrono::milliseconds elapsed);

    static constexpr auto splashFillDuration = std::chrono::milliseconds(1800);
    static constexpr auto splashDuration = std::chrono::milliseconds(2000);
    static constexpr std::size_t splashSegmentCount = 12;

    IPlatformAdapter& platform_;
    IKeyboardAdapter& keyboard_;
    IDisplayAdapter& display_;
    Logger& logger_;
    const BuildInfo& buildInfo_;
    InputEvents inputEvents_;
    std::chrono::milliseconds splashElapsed_{0};
    std::size_t filledSplashSegments_ = 0;
    bool started_ = false;
};

} // namespace cardputer_hub::core
