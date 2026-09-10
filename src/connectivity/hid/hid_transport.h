#pragma once

#include <array>
#include <cstdint>
#include <variant>

namespace cardputer_hub::connectivity {

struct HidKeyboardReport {
    std::uint8_t modifiers = 0;
    std::array<std::uint8_t, 6> usages{};

    static constexpr HidKeyboardReport neutral() noexcept { return {}; }
};

constexpr bool operator==(const HidKeyboardReport& left, const HidKeyboardReport& right) noexcept {
    return left.modifiers == right.modifiers && left.usages == right.usages;
}

struct HidConsumerReport {
    std::uint16_t usage = 0;

    static constexpr HidConsumerReport neutral() noexcept { return {}; }
};

constexpr bool operator==(HidConsumerReport left, HidConsumerReport right) noexcept {
    return left.usage == right.usage;
}

using HidReport = std::variant<HidKeyboardReport, HidConsumerReport>;

inline constexpr std::uint8_t keyboardHidReportId = 1;
inline constexpr std::uint8_t consumerHidReportId = 2;
extern const std::array<std::uint8_t, 90> hidReportDescriptor;

enum class HidTransportState : std::uint8_t { Unavailable, Starting, Ready, Busy, Error };
enum class HidSendResult : std::uint8_t { Sent, NotReady, Busy, AdapterError };
enum class HidPhysicalLinkState : std::uint8_t { Disconnected, Connected, Suspended };

bool isValidHidReport(const HidReport& report) noexcept;
bool isNeutralHidReport(const HidReport& report) noexcept;

class IHidTransport {
  public:
    virtual ~IHidTransport() = default;

    virtual HidTransportState state() const noexcept = 0;
    virtual HidSendResult send(const HidReport& report) = 0;
    virtual HidSendResult releaseAll() = 0;
};

class IUsbHidTransport : public IHidTransport {
  public:
    virtual HidPhysicalLinkState linkState() const noexcept = 0;
};

} // namespace cardputer_hub::connectivity
