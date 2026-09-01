#include "core/lifecycle/system_runtime.h"

namespace cardputer_hub::core {
namespace {

constexpr RgbColor black{0, 0, 0};
constexpr RgbColor white{255, 255, 255};
constexpr TextStyle productNameStyle{white, black, 2};
constexpr TextStyle versionStyle{white, black, 1};

} // namespace

SystemRuntime::SystemRuntime(IPlatformAdapter& platform, IKeyboardAdapter& keyboard,
                             IDisplayAdapter& display, Logger& logger,
                             const BuildInfo& buildInfo) noexcept
    : platform_(platform), keyboard_(keyboard), display_(display), logger_(logger),
      buildInfo_(buildInfo) {}

void SystemRuntime::start() {
    if (started_) {
        return;
    }

    platform_.begin();
    logger_.info("firmware.name", buildInfo_.name);
    logger_.info("firmware.version", buildInfo_.version);
    logger_.info("firmware.commit", buildInfo_.commit);
    logger_.info("firmware.build_type", buildInfo_.buildType);
    display_.clear(black);
    display_.drawText({8, 8}, buildInfo_.name, productNameStyle);
    display_.drawText({8, 32}, buildInfo_.version, versionStyle);
    started_ = true;
}

const InputEvents& SystemRuntime::update() {
    inputEvents_.clear();
    if (!started_) {
        return inputEvents_;
    }

    platform_.update();
    keyboard_.poll(inputEvents_);
    return inputEvents_;
}

} // namespace cardputer_hub::core
