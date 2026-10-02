#pragma once

#include "services/inventory/inventory_id.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cardputer_hub::services {

// One container record, stored as UTF-8 JSON:
//   {"schema":2,"id":"<32 hex>","revision":N,"name":"...","description":"..."}
// The name is a short title; the description is free text with line breaks
// that says what the container holds. Firmware and Companion enforce the same
// limits, counted in code points.
inline constexpr std::uint32_t inventorySchemaVersion = 2;
inline constexpr std::size_t inventoryMaxRecordBytes = 4096;
inline constexpr std::size_t inventoryMaxNameLength = 32;
inline constexpr std::size_t inventoryMaxDescriptionLength = 900;

struct InventoryRecord {
    InventoryId id{};
    std::string name;
    // May be empty. Line breaks are "\n".
    std::string description;
    // 1 for a new record; every committed edit adds exactly one.
    std::uint32_t revision = 1;
};

enum class InventoryRecordError : std::uint8_t {
    None,
    // Not well-formed JSON, or not the record's object shape.
    Malformed,
    InvalidUtf8,
    UnknownField,
    DuplicateField,
    MissingField,
    WrongSchema,
    BadId,
    BadRevision,
    BadName,
    BadDescription,
    TooLarge,
};

struct InventoryRecordDecode {
    std::optional<InventoryRecord> record;
    InventoryRecordError error = InventoryRecordError::None;
};

// Name text: valid UTF-8, 1..limit code points, no control or line separator
// characters, and no leading or trailing space.
[[nodiscard]] bool isValidInventoryText(std::string_view text, std::size_t maxCodePoints);
// Description text: valid UTF-8, 0..limit code points; line breaks ("\n") are
// the only control characters; no leading or trailing space or line break.
[[nodiscard]] bool isValidInventoryDescription(std::string_view text, std::size_t maxCodePoints);
[[nodiscard]] InventoryRecordError validateInventoryRecord(const InventoryRecord& record);

[[nodiscard]] InventoryRecordDecode decodeInventoryRecord(std::string_view json);
// Canonical encoding; nullopt when the record is invalid or encodes larger
// than inventoryMaxRecordBytes.
[[nodiscard]] std::optional<std::string> encodeInventoryRecord(const InventoryRecord& record);

} // namespace cardputer_hub::services
