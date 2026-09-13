#pragma once

#include "core/audio/audio_adapter.h"

namespace cardputer_hub::hardware {

class CardputerAudioAdapter final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t volumePercent) override;
    void setVolume(std::uint8_t volumePercent) override;
    bool play(const core::AudioClip& clip) override;

  private:
    bool started_ = false;
};

} // namespace cardputer_hub::hardware
