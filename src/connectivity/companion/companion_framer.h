#pragma once

#include "connectivity/companion/companion_protocol.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cardputer_hub::connectivity {

inline constexpr std::size_t companionMaxChunks = 16;
inline constexpr std::size_t companionChunkHeaderSize = 3;
inline constexpr std::size_t companionDefaultChunkPayload = 17;
inline constexpr auto companionReassemblyTimeout = std::chrono::seconds(2);

struct CompanionChunk {
    std::array<std::uint8_t, companionMaxMessageSize + companionChunkHeaderSize> bytes{};
    std::uint16_t size = 0;
};

class CompanionFramer {
  public:
    void reset() noexcept;
    void update(std::chrono::milliseconds elapsed);
    std::uint8_t nextMessageId() noexcept;
    bool encode(const CompanionEncodedMessage& message, std::size_t maxPayloadPerChunk,
                CompanionChunk* chunks, std::uint8_t& count, std::uint8_t capacity) noexcept;
    bool ingest(const std::uint8_t* data, std::size_t size);
    std::optional<CompanionEncodedMessage> take();

  private:
    void abandon() noexcept;
    bool assemble() noexcept;

    std::array<std::array<std::uint8_t, companionMaxMessageSize>, companionMaxChunks> parts_{};
    std::array<std::uint16_t, companionMaxChunks> partSizes_{};
    std::uint8_t nextOutgoingId_ = 1;
    std::uint8_t messageId_ = 0;
    std::uint8_t chunkCount_ = 0;
    std::uint16_t receivedMask_ = 0;
    bool assembling_ = false;
    std::chrono::milliseconds elapsed_{0};
    std::optional<CompanionEncodedMessage> complete_{};
};

} // namespace cardputer_hub::connectivity
