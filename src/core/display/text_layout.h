#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace cardputer_hub::core {

constexpr std::int32_t headerStatusRight = 234;
constexpr float systemTextScale = 1.20f;
constexpr float systemGlyphNativeWidth = 6.0f;
constexpr float systemGlyphNativeHeight = 8.0f;

inline std::int32_t textWidth(const char* text, float scale) {
    return static_cast<std::int32_t>(
        std::ceil(std::char_traits<char>::length(text) * systemGlyphNativeWidth * scale));
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
    if (text.size() <= maximum)
        return text;
    if (maximum <= 3)
        return text.substr(0, maximum);
    return text.substr(0, maximum - 3) + "...";
}

inline std::int32_t rightAlignedTextX(const char* text,
                                      std::int32_t rightEdge = headerStatusRight) {
    return rightEdge - systemTextWidth(text);
}

} // namespace cardputer_hub::core
