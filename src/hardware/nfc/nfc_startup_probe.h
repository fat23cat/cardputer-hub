#pragma once

#include <cstdint>

namespace cardputer_hub::hardware {

// Keep the one-time Grove probe ahead of Puzzle initialization. The NFC chip
// may still be starting when M5Unified has already configured the I2C bus.
template <typename Probe, typename Delay> bool probeNfcAtStartup(Probe&& probe, Delay&& delay) {
    delay(std::uint32_t{50});
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (probe())
            return true;
        if (attempt < 4)
            delay(std::uint32_t{20});
    }
    return false;
}

} // namespace cardputer_hub::hardware
