#pragma once

#include <cstddef>
#include <cstdint>

namespace cardputer_hub::core {

enum class MicrophoneReadResult { Pending, Ready, Error };

class IMicrophoneAdapter {
  public:
    virtual ~IMicrophoneAdapter() = default;
    virtual bool begin(std::uint32_t sampleRate) = 0;
    virtual void end() = 0;
    // Ready guarantees a complete window in samples. Error means capture is
    // no longer usable, including failure to schedule the next window.
    virtual MicrophoneReadResult read(std::int16_t* samples, std::size_t sampleCount) = 0;
};

} // namespace cardputer_hub::core
