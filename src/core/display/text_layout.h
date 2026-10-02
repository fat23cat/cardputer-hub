#pragma once

#include "core/text/utf8.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cardputer_hub::core {

constexpr std::int32_t headerStatusRight = 234;
constexpr float systemTextScale = 1.20f;
constexpr float systemGlyphNativeWidth = 6.0f;
constexpr float systemGlyphNativeHeight = 8.0f;

// Text is UTF-8. Every code point is one fixed-width glyph of the system font
// (see display_glyphs.h), so widths count code points, not bytes.
inline std::int32_t textWidth(const char* text, float scale) {
    return static_cast<std::int32_t>(std::ceil(utf8Length(text) * systemGlyphNativeWidth * scale));
}

inline std::int32_t systemTextWidth(const char* text) { return textWidth(text, systemTextScale); }

inline std::int32_t systemTextHeight() {
    return static_cast<std::int32_t>(std::ceil(systemGlyphNativeHeight * systemTextScale));
}

inline std::int32_t centeredTextX(const char* text, std::int32_t left, std::int32_t width,
                                  float scale = systemTextScale) {
    return left + (width - textWidth(text, scale)) / 2;
}

inline std::size_t systemTextMaxCharacters(std::int32_t availableWidth) {
    if (availableWidth <= 0)
        return 0;
    return static_cast<std::size_t>(availableWidth / (systemGlyphNativeWidth * systemTextScale));
}

inline std::string fitSystemText(const std::string& text, std::int32_t availableWidth) {
    const auto maximum = systemTextMaxCharacters(availableWidth);
    if (utf8Length(text) <= maximum)
        return text;
    if (maximum <= 3)
        return utf8Prefix(text, maximum);
    return utf8Prefix(text, maximum - 3) + "...";
}

inline std::int32_t rightAlignedTextX(const char* text,
                                      std::int32_t rightEdge = headerStatusRight) {
    return rightEdge - systemTextWidth(text);
}

// Word-wraps UTF-8 text into lines of at most `columns` glyphs. Spaces separate
// words; a word longer than a line is split at a code-point boundary.
std::vector<std::string> wrapText(std::string_view text, std::size_t columns);

} // namespace cardputer_hub::core
