#pragma once

#include <cstdint>

namespace cardputer_hub::core {

// Why the device last started, reduced to what a user can act on. Power-on,
// software restart and deep-sleep wake are Normal.
enum class ResetReason : std::uint8_t {
    Normal,
    // A crash: exception, assert or abort (for example a failed allocation).
    Panic,
    // A task or interrupt watchdog fired: something blocked too long.
    Watchdog,
    // The supply voltage dropped too low.
    Brownout,
    Unknown,
};

// "PANIC", "WATCHDOG", "BROWNOUT" or "UNKNOWN"; empty for Normal.
[[nodiscard]] constexpr const char* resetReasonLabel(ResetReason reason) noexcept {
    switch (reason) {
    case ResetReason::Normal:
        return "";
    case ResetReason::Panic:
        return "PANIC";
    case ResetReason::Watchdog:
        return "WATCHDOG";
    case ResetReason::Brownout:
        return "BROWNOUT";
    case ResetReason::Unknown:
        break;
    }
    return "UNKNOWN";
}

} // namespace cardputer_hub::core
