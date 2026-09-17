#pragma once

#include "core/display/display_adapter.h"

namespace cardputer_hub::apps {

inline constexpr std::int32_t launcherHeaderHeight = 21;
inline constexpr std::int32_t launcherRowTop = 27;
inline constexpr std::int32_t launcherRowHeight = 36;
inline constexpr std::int32_t launcherRowWidth = 228;
inline constexpr std::int32_t launcherRowLeft = 6;
inline constexpr std::int32_t launcherIconX = 32;
inline constexpr std::int32_t launcherNameX = 52;
inline constexpr std::int32_t launcherDotX = 225;

void drawAppIcon(core::IDisplayAdapter& display, core::PixelPosition position, const char* iconId,
                 core::RgbColor color);
void drawAvailabilityDot(core::IDisplayAdapter& display, core::PixelPosition position,
                         bool available);

} // namespace cardputer_hub::apps
