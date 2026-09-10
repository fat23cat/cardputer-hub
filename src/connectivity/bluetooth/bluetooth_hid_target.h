#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "connectivity/hid/hid_transport.h"

namespace cardputer_hub::connectivity {

struct BluetoothBondReference {
    std::array<std::uint8_t, 16> bytes{};
};

constexpr bool operator==(const BluetoothBondReference& left,
                          const BluetoothBondReference& right) noexcept {
    for (std::size_t index = 0; index < left.bytes.size(); ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return false;
        }
    }
    return true;
}

constexpr bool operator!=(const BluetoothBondReference& left,
                          const BluetoothBondReference& right) noexcept {
    return !(left == right);
}

constexpr bool operator<(const BluetoothBondReference& left,
                         const BluetoothBondReference& right) noexcept {
    for (std::size_t index = 0; index < left.bytes.size(); ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return left.bytes[index] < right.bytes[index];
        }
    }
    return false;
}

enum class BluetoothBondSelectionResult : std::uint8_t {
    Selected,
    Cleared,
    AlreadySelected,
    NotFound,
    Disabled,
    AdapterError,
};

class IBluetoothHidTarget {
  public:
    virtual ~IBluetoothHidTarget() = default;

    virtual BluetoothBondSelectionResult
    selectBond(std::optional<BluetoothBondReference> reference) = 0;
    virtual std::optional<BluetoothBondReference> selectedBond() const noexcept = 0;
    virtual IHidTransport& hidTransport() noexcept = 0;
};

} // namespace cardputer_hub::connectivity
