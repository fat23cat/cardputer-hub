#pragma once

#include "core/display/display_adapter.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <string>

namespace cardputer_hub::core {

inline constexpr std::int32_t screenHeaderMarginX = 6;
inline constexpr std::int32_t screenHeaderTextY = 6;
inline constexpr std::int32_t screenHeaderRuleY = 20;
inline constexpr std::int32_t screenHeaderHeight = screenHeaderRuleY + 1;

// The standard screen header: an Ink title at the left, an optional quiet
// status at the right and a one-pixel Ink rule beneath both. The title is
// shortened before it would touch the status. Callers own the background.
inline void drawScreenHeader(IDisplayAdapter& display, const std::string& title,
                             const std::string& status = {},
                             RgbColor statusColor = palette::ordinal) {
    const auto statusWidth = status.empty() ? 0 : systemTextWidth(status.c_str()) + 8;
    const auto shown = fitSystemText(title, headerStatusRight - screenHeaderMarginX - statusWidth);
    display.drawText({screenHeaderMarginX, screenHeaderTextY}, shown.c_str(),
                     {palette::ink, palette::bone, systemTextScale});
    if (!status.empty())
        display.drawText({rightAlignedTextX(status.c_str()), screenHeaderTextY}, status.c_str(),
                         {statusColor, palette::bone, systemTextScale});
    display.fillRectangle({screenHeaderMarginX, screenHeaderRuleY},
                          headerStatusRight - screenHeaderMarginX, 1, palette::ink);
}

} // namespace cardputer_hub::core
