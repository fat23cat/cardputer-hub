#pragma once

#include <cstddef>
#include <cstdint>

namespace cardputer_hub::core {

struct AudioClip {
    const std::int16_t* samples = nullptr;
    std::size_t sampleCount = 0;
    std::uint32_t sampleRate = 0;
};

class IAudioAdapter {
  public:
    virtual ~IAudioAdapter() = default;
    virtual bool begin(std::uint8_t volumePercent) = 0;
    virtual void end() = 0;
    virtual void setVolume(std::uint8_t volumePercent) = 0;
    virtual bool isPlaying() const = 0;
    virtual bool play(const AudioClip& clip) = 0;
};

} // namespace cardputer_hub::core
