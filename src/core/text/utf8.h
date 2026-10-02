#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace cardputer_hub::core {

// Strict UTF-8 (RFC 3629): shortest form only, no surrogates, at most U+10FFFF.

// Decodes the code point at `position` and advances past it. Malformed input
// returns nullopt and advances by one byte, so callers can resynchronise.
std::optional<char32_t> decodeUtf8(std::string_view text, std::size_t& position);
[[nodiscard]] bool isValidUtf8(std::string_view text);
// Code points; each malformed byte counts as one.
[[nodiscard]] std::size_t utf8Length(std::string_view text);
// The first `codePoints` code points, never splitting one.
[[nodiscard]] std::string utf8Prefix(std::string_view text, std::size_t codePoints);
void appendUtf8(std::string& output, char32_t codePoint);

} // namespace cardputer_hub::core
