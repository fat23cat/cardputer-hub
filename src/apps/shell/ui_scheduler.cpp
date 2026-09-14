#include "apps/shell/ui_scheduler.h"

#include <algorithm>

namespace cardputer_hub::apps {

std::optional<std::chrono::milliseconds>
UiScheduler::elapsedForUpdate(std::chrono::milliseconds elapsed, bool hasInput) {
    elapsedSinceUpdate_ += std::max(elapsed, std::chrono::milliseconds(0));
    if (!hasInput && elapsedSinceUpdate_ < updateInterval)
        return std::nullopt;

    const auto accumulated = elapsedSinceUpdate_;
    elapsedSinceUpdate_ = std::chrono::milliseconds(0);
    return accumulated;
}

} // namespace cardputer_hub::apps
