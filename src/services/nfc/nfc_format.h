#pragma once

#include "core/nfc/nfc_reader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cardputer_hub::services {

// Presentation text for normalized NFC identification. Capitalised for the LCD,
// ASCII only, owned by the project rather than by any vendor library.
[[nodiscard]] const char* nfcTechnologyName(core::NfcTechnology technology) noexcept;
[[nodiscard]] const char* nfcCardTypeName(core::NfcCardType type) noexcept;

// "04 A8 12 93"
[[nodiscard]] std::string nfcHexSpaced(const std::uint8_t* bytes, std::size_t count);
[[nodiscard]] std::string nfcHexSpaced(const std::vector<std::uint8_t>& bytes);
// "04A81293"
[[nodiscard]] std::string nfcHexCompact(const std::uint8_t* bytes, std::size_t count);
// Two bytes, most significant first: 0x0004 -> "00 04"
[[nodiscard]] std::string nfcAtqaText(std::uint16_t atqa);
// One byte: 0x08 -> "08"
[[nodiscard]] std::string nfcByteText(std::uint8_t value);

} // namespace cardputer_hub::services
