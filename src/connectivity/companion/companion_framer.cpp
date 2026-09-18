#include "connectivity/companion/companion_framer.h"

#include <cstring>

namespace cardputer_hub::connectivity {
namespace {

std::uint8_t chunkCountFor(std::uint16_t size, std::size_t maxPayloadPerChunk) noexcept {
    if (size == 0 || maxPayloadPerChunk == 0) {
        return 0;
    }
    const auto count =
        (static_cast<std::size_t>(size) + maxPayloadPerChunk - 1) / maxPayloadPerChunk;
    if (count > companionMaxChunks) {
        return 0;
    }
    return static_cast<std::uint8_t>(count);
}

std::uint16_t completeMask(std::uint8_t chunkCount) noexcept {
    if (chunkCount == 16) {
        return 0xFFFF;
    }
    return static_cast<std::uint16_t>((1U << chunkCount) - 1U);
}

} // namespace

void CompanionFramer::reset() noexcept {
    abandon();
    complete_.reset();
}

void CompanionFramer::abandon() noexcept {
    parts_ = {};
    partSizes_ = {};
    messageId_ = 0;
    chunkCount_ = 0;
    receivedMask_ = 0;
    assembling_ = false;
    elapsed_ = {};
}

void CompanionFramer::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero()) {
        elapsed = std::chrono::milliseconds::zero();
    }
    if (!assembling_) {
        return;
    }
    if (elapsed >= companionReassemblyTimeout - elapsed_) {
        abandon();
        return;
    }
    elapsed_ += elapsed;
}

std::uint8_t CompanionFramer::nextMessageId() noexcept {
    const auto id = nextOutgoingId_;
    nextOutgoingId_ = nextOutgoingId_ == 255 ? 1 : static_cast<std::uint8_t>(nextOutgoingId_ + 1);
    return id;
}

std::uint8_t CompanionFramer::encodedChunkCount(std::uint16_t size,
                                                std::size_t maxPayloadPerChunk) noexcept {
    return chunkCountFor(size, maxPayloadPerChunk);
}

bool CompanionFramer::encodeChunk(const CompanionEncodedMessage& message,
                                  std::size_t maxPayloadPerChunk, std::uint8_t messageId,
                                  std::uint8_t index, CompanionChunk& chunk) noexcept {
    chunk = {};
    const auto chunkCount = chunkCountFor(message.size, maxPayloadPerChunk);
    if (chunkCount == 0 || index >= chunkCount || messageId == 0 ||
        message.size > companionMaxMessageSize || maxPayloadPerChunk == 0) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(index) * maxPayloadPerChunk;
    const auto remaining = static_cast<std::size_t>(message.size) - offset;
    const auto payload = remaining < maxPayloadPerChunk ? remaining : maxPayloadPerChunk;
    chunk.size = static_cast<std::uint16_t>(companionChunkHeaderSize + payload);
    chunk.bytes[0] = messageId;
    chunk.bytes[1] = index;
    chunk.bytes[2] = chunkCount;
    std::memcpy(chunk.bytes.data() + companionChunkHeaderSize, message.bytes.data() + offset,
                payload);
    return true;
}

bool CompanionFramer::encode(const CompanionEncodedMessage& message, std::size_t maxPayloadPerChunk,
                             CompanionChunk* chunks, std::uint8_t& count,
                             std::uint8_t capacity) noexcept {
    count = 0;
    if (chunks == nullptr || message.size == 0 || message.size > companionMaxMessageSize ||
        maxPayloadPerChunk == 0) {
        return false;
    }
    const auto chunkCount = chunkCountFor(message.size, maxPayloadPerChunk);
    if (chunkCount == 0 || chunkCount > capacity) {
        return false;
    }
    const auto messageId = nextMessageId();
    for (std::uint8_t index = 0; index < chunkCount; ++index) {
        if (!encodeChunk(message, maxPayloadPerChunk, messageId, index, chunks[index])) {
            count = 0;
            return false;
        }
    }
    count = chunkCount;
    return true;
}

bool CompanionFramer::assemble() noexcept {
    std::uint16_t stride = 0;
    for (std::uint8_t index = 0; index + 1 < chunkCount_; ++index) {
        if (partSizes_[index] == 0) {
            return false;
        }
        if (stride == 0) {
            stride = partSizes_[index];
        } else if (partSizes_[index] != stride) {
            return false;
        }
    }
    if (chunkCount_ > 1 && (partSizes_[chunkCount_ - 1] == 0 ||
                            (stride != 0 && partSizes_[chunkCount_ - 1] > stride))) {
        return false;
    }
    std::size_t total = 0;
    CompanionEncodedMessage message{};
    for (std::uint8_t index = 0; index < chunkCount_; ++index) {
        if (total + partSizes_[index] > companionMaxMessageSize) {
            return false;
        }
        std::memcpy(message.bytes.data() + total, parts_[index].data(), partSizes_[index]);
        total += partSizes_[index];
    }
    if (total == 0) {
        return false;
    }
    message.size = static_cast<std::uint16_t>(total);
    complete_ = message;
    return true;
}

bool CompanionFramer::ingest(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < companionChunkHeaderSize) {
        return false;
    }
    const auto messageId = data[0];
    const auto index = data[1];
    const auto chunkCount = data[2];
    const auto payloadSize = static_cast<std::uint16_t>(size - companionChunkHeaderSize);
    if (messageId == 0 || chunkCount == 0 || chunkCount > companionMaxChunks ||
        index >= chunkCount || payloadSize == 0) {
        return false;
    }
    if (assembling_ && messageId != messageId_) {
        abandon();
    }
    if (!assembling_) {
        if (complete_.has_value()) {
            return false;
        }
        assembling_ = true;
        messageId_ = messageId;
        chunkCount_ = chunkCount;
        receivedMask_ = 0;
        elapsed_ = {};
        parts_ = {};
        partSizes_ = {};
    } else if (chunkCount != chunkCount_) {
        abandon();
        return false;
    }
    const auto bit = static_cast<std::uint16_t>(1U << index);
    if ((receivedMask_ & bit) != 0) {
        abandon();
        return false;
    }
    std::memcpy(parts_[index].data(), data + companionChunkHeaderSize, payloadSize);
    partSizes_[index] = payloadSize;
    receivedMask_ |= bit;
    if (receivedMask_ != completeMask(chunkCount_)) {
        return true;
    }
    const auto assembled = assemble();
    abandon();
    assembling_ = false;
    return assembled;
}

std::optional<CompanionEncodedMessage> CompanionFramer::take() {
    auto message = complete_;
    complete_.reset();
    return message;
}

} // namespace cardputer_hub::connectivity
