#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cardputer_hub::core {

// The system font is a 6x8 glyph table with one byte per glyph. ASCII keeps its
// codes; the upper half holds Russian Cyrillic and a few typographic marks, so
// Companion-edited UTF-8 text renders on the Cardputer. Layout measures and
// wraps text by these glyphs, and display adapters draw exactly these bytes.
namespace display_glyph {
inline constexpr std::uint8_t cyrillicCapitalA = 0x80;  // U+0410..U+042F
inline constexpr std::uint8_t cyrillicSmallA = 0xA0;    // U+0430..U+044F
inline constexpr std::uint8_t cyrillicCapitalIo = 0xC0; // U+0401
inline constexpr std::uint8_t cyrillicSmallIo = 0xC1;   // U+0451
inline constexpr std::uint8_t leftGuillemet = 0xC2;     // U+00AB
inline constexpr std::uint8_t rightGuillemet = 0xC3;    // U+00BB
inline constexpr std::uint8_t numeroSign = 0xC4;        // U+2116
inline constexpr std::uint8_t degreeSign = 0xC5;        // U+00B0
inline constexpr std::uint8_t ellipsis = 0xC6;          // U+2026
// Any other character, and every malformed UTF-8 byte.
inline constexpr std::uint8_t unsupported = 0x7F;
inline constexpr std::uint8_t lastDefined = ellipsis;
} // namespace display_glyph

// One glyph byte per code point (and per malformed byte).
[[nodiscard]] std::string displayGlyphs(std::string_view utf8);

} // namespace cardputer_hub::core
