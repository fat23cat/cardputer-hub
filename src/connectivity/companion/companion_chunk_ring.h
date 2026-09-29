#pragma once

#include "connectivity/companion/companion_framer.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cardputer_hub::connectivity {

// Received-chunk FIFO between the BLE host task and the main loop. Enough for
// two full AI_USAGE messages in 17-byte chunks (32 × 22 bytes) or five
// MTU-sized chunks, at a fraction of 32 fixed 259-byte slots.
inline constexpr std::size_t companionIncomingRingBytes = 1536;

// A byte ring of variable-length records (2-byte length, then the chunk), so a
// small chunk uses only its own size. Not synchronised; the caller locks.
template <std::size_t Bytes> class CompanionChunkRing {
  public:
    static constexpr std::size_t recordHeaderSize = 2;

    // False when the chunk is empty, larger than a chunk, or does not fit.
    bool push(const std::uint8_t* data, std::size_t size) noexcept {
        const auto maximum = CompanionChunk{}.bytes.size();
        if (data == nullptr || size == 0 || size > maximum ||
            used_ + recordHeaderSize + size > Bytes)
            return false;
        const std::uint8_t header[recordHeaderSize] = {static_cast<std::uint8_t>(size),
                                                       static_cast<std::uint8_t>(size >> 8U)};
        write(header, recordHeaderSize);
        write(data, size);
        ++count_;
        return true;
    }

    bool pop(CompanionChunk& chunk) noexcept {
        if (count_ == 0)
            return false;
        std::uint8_t header[recordHeaderSize]{};
        read(header, recordHeaderSize);
        const auto size = static_cast<std::uint16_t>(header[0] | (header[1] << 8U));
        read(chunk.bytes.data(), size);
        chunk.size = size;
        --count_;
        return true;
    }

    void clear() noexcept {
        head_ = 0;
        used_ = 0;
        count_ = 0;
    }

    std::size_t size() const noexcept { return count_; }

  private:
    void write(const std::uint8_t* data, std::size_t size) noexcept {
        auto tail = (head_ + used_) % Bytes;
        for (std::size_t index = 0; index < size; ++index) {
            bytes_[tail] = data[index];
            tail = tail + 1 == Bytes ? 0 : tail + 1;
        }
        used_ += size;
    }

    void read(std::uint8_t* data, std::size_t size) noexcept {
        for (std::size_t index = 0; index < size; ++index) {
            data[index] = bytes_[head_];
            head_ = head_ + 1 == Bytes ? 0 : head_ + 1;
        }
        used_ -= size;
    }

    std::array<std::uint8_t, Bytes> bytes_{};
    std::size_t head_ = 0;
    std::size_t used_ = 0;
    std::size_t count_ = 0;
};

} // namespace cardputer_hub::connectivity
