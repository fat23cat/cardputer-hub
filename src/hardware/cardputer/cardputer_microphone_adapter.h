#pragma once

#include "core/audio/microphone_adapter.h"

#include <array>

#include "driver/i2s_types.h"

namespace cardputer_hub::hardware {

class CardputerMicrophoneAdapter final : public core::IMicrophoneAdapter {
  public:
    static constexpr std::size_t windowSamples = 256;

    bool begin(std::uint32_t sampleRate) override;
    void end() override;
    core::MicrophoneReadResult read(std::int16_t* samples, std::size_t sampleCount) override;

  private:
    static constexpr std::size_t stereoWords = windowSamples * 2;
    std::array<std::int32_t, stereoWords> buffer_{};
    i2s_chan_handle_t channel_ = nullptr;
    std::size_t bufferedWords_ = 0;
};

} // namespace cardputer_hub::hardware
