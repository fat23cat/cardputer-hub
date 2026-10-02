#pragma once

#include "services/nfc/nfc_models.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cardputer_hub::services {

// NFC Forum Type 2 tag inspection and NDEF encoding. Pure functions over bytes,
// so tag content handling is tested without a reader.

// Inspection starts at page 2 (static lock bytes, then the capability container
// at page 3). The first window spans 16 pages: 8 header bytes and the first 56
// bytes of the data area, which decide a factory-formatted tag.
inline constexpr std::uint16_t nfcInspectFirstPage = 2;
inline constexpr std::size_t nfcInspectPages = 16;
inline constexpr std::size_t nfcInspectBytes = nfcInspectPages * 4;

// Classify bytes read from page 2. Blank needs proof: a Terminator TLV, or the
// whole data area (as declared by the capability container) read and holding
// nothing but NULL bytes and at most one empty NDEF TLV. Bytes that end before
// a decision report `incomplete`. A message that continues past the data area
// is OtherData, never truncated.
[[nodiscard]] NfcTagInspection inspectType2Window(const std::uint8_t* window, std::size_t size);

// The bytes to write from the first user page: NDEF TLV, message, terminator,
// zero-padded to whole pages. Nullopt when the result exceeds `capacity`.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encodeType2NdefArea(const std::vector<std::uint8_t>& message, std::size_t capacity);

// One short NDEF Text record (well-known type "T", UTF-8) as a whole message.
[[nodiscard]] std::vector<std::uint8_t> encodeNdefTextMessage(std::string_view text,
                                                              std::string_view language = "en");
// The text of a message that is exactly one UTF-8 Text record; nullopt for
// anything else.
[[nodiscard]] std::optional<std::string>
decodeNdefTextMessage(const std::vector<std::uint8_t>& message);

} // namespace cardputer_hub::services
