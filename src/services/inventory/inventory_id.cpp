#include "services/inventory/inventory_id.h"

#include "services/nfc/nfc_ndef.h"

#include <algorithm>

namespace cardputer_hub::services {
namespace {

constexpr char hexDigits[] = "0123456789abcdef";

int hexValue(char digit) {
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    return -1;
}

bool isNull(const InventoryId& id) {
    return std::all_of(id.begin(), id.end(), [](std::uint8_t byte) { return byte == 0; });
}

} // namespace

std::string inventoryIdHex(const InventoryId& id) {
    std::string text;
    text.reserve(inventoryIdHexLength);
    for (const auto byte : id) {
        text.push_back(hexDigits[byte >> 4U]);
        text.push_back(hexDigits[byte & 0x0FU]);
    }
    return text;
}

std::optional<InventoryId> parseInventoryIdHex(std::string_view text) {
    if (text.size() != inventoryIdHexLength)
        return std::nullopt;
    InventoryId id{};
    for (std::size_t index = 0; index < id.size(); ++index) {
        const int high = hexValue(text[index * 2]);
        const int low = hexValue(text[index * 2 + 1]);
        if (high < 0 || low < 0)
            return std::nullopt;
        id[index] = static_cast<std::uint8_t>(high << 4 | low);
    }
    if (isNull(id))
        return std::nullopt;
    return id;
}

InventoryId generateInventoryId(core::IRandomSource& random) {
    InventoryId id{};
    do {
        random.fill(id.data(), id.size());
    } while (isNull(id));
    return id;
}

std::vector<std::uint8_t> inventoryTagMessage(const InventoryId& id) {
    return encodeNdefTextMessage(std::string(inventoryTagPrefix) + inventoryIdHex(id));
}

InventoryTagContent parseInventoryTag(const std::vector<std::uint8_t>& message) {
    InventoryTagContent content;
    const auto text = decodeNdefTextMessage(message);
    if (!text || text->rfind(inventoryTagFamily, 0) != 0)
        return content;
    content.kind = InventoryTagKind::UnsupportedVersion;
    const std::string_view prefix(inventoryTagPrefix);
    if (text->rfind(prefix, 0) != 0)
        return content;
    const auto id = parseInventoryIdHex(std::string_view(*text).substr(prefix.size()));
    if (!id)
        return content;
    content.kind = InventoryTagKind::Inventory;
    content.id = *id;
    return content;
}

} // namespace cardputer_hub::services
