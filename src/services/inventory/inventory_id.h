#pragma once

#include "core/platform/random_source.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cardputer_hub::services {

// A random 128-bit locator written to an inventory tag. It names a record file;
// it is not a secret and authorizes nothing. Copying it to another tag makes
// both tags open the same record.
using InventoryId = std::array<std::uint8_t, 16>;

inline constexpr std::size_t inventoryIdHexLength = 32;
// NDEF Text content of an inventory tag: this prefix and 32 lowercase hex digits.
inline constexpr char inventoryTagPrefix[] = "CHINV1:";
// Any later tag format keeps this family prefix and changes the version digit.
inline constexpr char inventoryTagFamily[] = "CHINV";

[[nodiscard]] std::string inventoryIdHex(const InventoryId& id);
// Exactly 32 lowercase hex digits; the all-zero ID is rejected.
[[nodiscard]] std::optional<InventoryId> parseInventoryIdHex(std::string_view text);
// A fresh random ID, never all zero.
[[nodiscard]] InventoryId generateInventoryId(core::IRandomSource& random);

// The NDEF message written to a tag for `id`.
[[nodiscard]] std::vector<std::uint8_t> inventoryTagMessage(const InventoryId& id);

enum class InventoryTagKind : std::uint8_t {
    Inventory,
    // An NDEF message that is not an inventory tag.
    NotInventory,
    // An inventory-family tag of another version or with a malformed ID.
    UnsupportedVersion,
};

struct InventoryTagContent {
    InventoryTagKind kind = InventoryTagKind::NotInventory;
    InventoryId id{};
};

[[nodiscard]] InventoryTagContent parseInventoryTag(const std::vector<std::uint8_t>& message);

} // namespace cardputer_hub::services
