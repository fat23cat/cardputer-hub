#pragma once

#include <array>
#include <cstdint>

#include "connectivity/hid/hid_transport.h"

namespace cardputer_hub::connectivity {

inline constexpr std::uint16_t nativeUsbVendorId = 0x303A;
inline constexpr std::uint16_t nativeUsbProductId = 0x4005;

alignas(4) extern const std::array<std::uint8_t, 18> nativeUsbDeviceDescriptor;
extern const std::array<std::uint8_t, 100> nativeUsbConfigurationDescriptor;
extern const std::array<std::uint8_t, 90>& nativeUsbHidReportDescriptor;
extern std::array<const char*, 5> nativeUsbStrings;

} // namespace cardputer_hub::connectivity
