#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cardputer_hub::services {

// One-second samples of the overview for sparklines, oldest first. A gap is a
// sample with no valid series; nothing here is persisted.
class MacStatusHistory {
  public:
    static constexpr std::size_t capacity = 60;

    struct Sample {
        bool cpuValid = false;
        std::uint8_t cpu = 0;
        bool networkValid = false;
        std::uint32_t downloadKiBps = 0;
        std::uint32_t uploadKiBps = 0;
    };

    void clear() noexcept {
        head_ = 0;
        size_ = 0;
        ++generation_;
    }

    void append(const Sample& sample) noexcept {
        samples_[(head_ + size_) % capacity] = sample;
        if (size_ < capacity)
            ++size_;
        else
            head_ = (head_ + 1) % capacity;
        ++generation_;
    }

    void appendGap() noexcept { append({}); }

    std::size_t size() const noexcept { return size_; }
    const Sample& at(std::size_t index) const noexcept {
        return samples_[(head_ + index) % capacity];
    }
    std::uint32_t generation() const noexcept { return generation_; }

  private:
    std::array<Sample, capacity> samples_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint32_t generation_ = 0;
};

} // namespace cardputer_hub::services
