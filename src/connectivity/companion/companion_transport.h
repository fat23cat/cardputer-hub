#pragma once

#include "connectivity/companion/companion_protocol.h"

#include <cstdint>
#include <optional>

namespace cardputer_hub::connectivity {

enum class CompanionTransportState : std::uint8_t { Unavailable, Ready };
enum class CompanionSendResult : std::uint8_t { Sent, NotReady, Busy, AdapterError };

struct CompanionPayload {
    std::array<std::uint8_t, companionMaxMessageSize> bytes{};
    std::uint16_t size = 0;
};

class ICompanionTransport {
  public:
    virtual ~ICompanionTransport() = default;
    virtual CompanionTransportState state() const noexcept = 0;
    virtual CompanionSendResult send(const CompanionPayload& payload) = 0;
    virtual std::optional<CompanionPayload> receive() = 0;
};

} // namespace cardputer_hub::connectivity
