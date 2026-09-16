#pragma once

#include <cstdint>
#include <string>

namespace cardputer_hub::core {

constexpr std::int32_t headerStatusRight = 234;
constexpr std::int32_t systemGlyphWidth = 6;

inline std::int32_t rightAlignedTextX(const char* text) {
    return headerStatusRight -
           static_cast<std::int32_t>(std::char_traits<char>::length(text)) * systemGlyphWidth;
}

} // namespace cardputer_hub::core
