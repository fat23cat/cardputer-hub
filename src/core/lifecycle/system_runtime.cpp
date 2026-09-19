#include "core/lifecycle/system_runtime.h"

#include <algorithm>

#include "core/display/palette.h"

namespace cardputer_hub::core {
namespace {

constexpr TextStyle productNameStyle{palette::ink, palette::bone, 2};
constexpr TextStyle quietStyle{palette::ordinal, palette::bone, 1};
constexpr TextStyle versionStyle{palette::ink, palette::bone, 1};

constexpr PixelPosition progressOrigin{18, 112};
constexpr std::int32_t progressSegmentWidth = 14;
constexpr std::int32_t progressSegmentHeight = 5;
constexpr std::int32_t progressSegmentGap = 3;

PixelPosition progressSegmentPosition(std::size_t index) {
    return {progressOrigin.x +
                static_cast<std::int32_t>(index) * (progressSegmentWidth + progressSegmentGap),
            progressOrigin.y};
}

} // namespace

SystemRuntime::SystemRuntime(IPlatformAdapter& platform, IKeyboardAdapter& keyboard,
                             IDisplayAdapter& display, DisplayPowerController& displayPower,
                             Logger& logger, const BuildInfo& buildInfo) noexcept
    : platform_(platform), keyboard_(keyboard), display_(display), displayPower_(displayPower),
      logger_(logger), buildInfo_(buildInfo) {}

void SystemRuntime::start() {
    if (started_) {
        return;
    }

    platform_.begin();
    displayPower_.captureNormalLevel();
    logger_.info("firmware.name", buildInfo_.name);
    logger_.info("firmware.version", buildInfo_.version);
    logger_.info("firmware.commit", buildInfo_.commit);
    logger_.info("firmware.build_type", buildInfo_.buildType);
    drawSplash();
    started_ = true;
}

void SystemRuntime::drawSplash() {
    display_.beginFrame();
    display_.clear(palette::bone);

    // The blue rail and restrained registration marks frame this sparse
    // full-screen state without carrying decorative chrome into normal views.
    display_.fillRectangle({0, 0}, 6, 135, palette::blue);
    display_.fillRectangle({18, 15}, 12, 1, palette::ink);
    display_.fillRectangle({18, 15}, 1, 8, palette::ink);
    display_.fillRectangle({219, 15}, 3, 3, palette::blue);
    display_.fillRectangle({219, 18}, 1, 5, palette::ink);

    display_.drawText({18, 24}, "SYSTEM STARTUP", quietStyle);
    display_.drawText({18, 43}, buildInfo_.name, productNameStyle);
    display_.drawText({18, 75}, "VERSION", quietStyle);
    display_.drawText({66, 75}, buildInfo_.version, versionStyle);

    for (std::size_t index = 0; index < splashSegmentCount; ++index) {
        display_.fillRectangle(progressSegmentPosition(index), progressSegmentWidth,
                               progressSegmentHeight, palette::pale);
    }
    display_.endFrame();
}

void SystemRuntime::advanceSplash(std::chrono::milliseconds elapsed) {
    if (splashFinished()) {
        return;
    }
    elapsed = std::max(elapsed, std::chrono::milliseconds::zero());
    const auto remaining = splashDuration - splashElapsed_;
    splashElapsed_ += std::min(elapsed, remaining);

    const auto fillElapsed = std::min(splashElapsed_, splashFillDuration);
    const auto targetSegments = static_cast<std::size_t>(
        fillElapsed.count() * static_cast<std::int64_t>(splashSegmentCount) /
        splashFillDuration.count());
    if (targetSegments == filledSplashSegments_) {
        return;
    }

    display_.beginFrame();
    while (filledSplashSegments_ < targetSegments) {
        display_.fillRectangle(progressSegmentPosition(filledSplashSegments_), progressSegmentWidth,
                               progressSegmentHeight, palette::blue);
        ++filledSplashSegments_;
    }
    display_.endFrame();
}

const InputEvents& SystemRuntime::update(std::chrono::milliseconds elapsed) {
    inputEvents_.clear();
    if (!started_) {
        return inputEvents_;
    }

    platform_.update();
    const auto poll = keyboard_.poll(inputEvents_);
    // The idle timer starts from the normal application UI, so the frame that
    // hands the splash off does not count as idle time.
    const bool displayPolicyActive = splashFinished();
    advanceSplash(elapsed);
    // Consuming here keeps wake-only input out of every screen and Mini App.
    if (displayPolicyActive && displayPower_.update(elapsed, poll.physicalPress)) {
        inputEvents_.clear();
    }
    return inputEvents_;
}

} // namespace cardputer_hub::core
