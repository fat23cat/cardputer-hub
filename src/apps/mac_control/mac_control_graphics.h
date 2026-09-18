#pragma once

#include "apps/mac_control/mac_control_bindings.h"
#include "core/display/display_adapter.h"

#include <cstdint>

namespace cardputer_hub::apps {

inline constexpr std::int32_t macControlWidth = 240;
inline constexpr std::int32_t macControlHeight = 135;
inline constexpr std::int32_t macControlColumnWidth = 80;
inline constexpr std::int32_t macControlRow0Height = 67;
inline constexpr std::int32_t macControlRow1Height = 68;
inline constexpr std::int32_t macControlSuccessHoldMs = 1500;
inline constexpr std::int32_t macControlFailureHoldMs = 2000;

struct MacControlTileRect {
    core::PixelPosition origin{};
    std::int32_t width = 0;
    std::int32_t height = 0;
};

[[nodiscard]] MacControlTileRect macControlTileRect(std::uint8_t slot);

void drawMacControlDigit(core::IDisplayAdapter& display, core::PixelPosition position, char digit,
                         core::RgbColor color);
void drawMacControlGrid(core::IDisplayAdapter& display, const MacControlPage& page);
void drawMacControlTakeover(core::IDisplayAdapter& display, MacControlTileRect tile,
                            core::RgbColor surface, std::uint8_t slot,
                            const MacControlBinding* binding);

} // namespace cardputer_hub::apps
