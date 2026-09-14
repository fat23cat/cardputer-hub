#pragma once

#include <chrono>
#include <optional>

namespace cardputer_hub::apps {

class UiScheduler {
  public:
    static constexpr std::chrono::milliseconds updateInterval{20};

    std::optional<std::chrono::milliseconds> elapsedForUpdate(std::chrono::milliseconds elapsed,
                                                              bool hasInput);

  private:
    std::chrono::milliseconds elapsedSinceUpdate_{0};
};

} // namespace cardputer_hub::apps
