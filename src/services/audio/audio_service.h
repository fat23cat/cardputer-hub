#pragma once

#include <cstddef>
#include <cstdint>

#include "core/actions/action_bus.h"
#include "core/audio/audio_adapter.h"
#include "services/configuration/configuration_service.h"

namespace cardputer_hub::services {

enum class AudioCue : std::uint8_t { KeyPress, StepLeft, StepRight };
enum class AudioResult : std::uint8_t { Success, InvalidVolume, StorageError, AdapterError };

class AudioService final : public core::IActionHandler {
  public:
    static constexpr std::uint32_t sampleRate = 16000;
    static constexpr std::size_t keyClipLength = 1280;
    static constexpr std::size_t stepClipLength = 1760;
    static constexpr std::size_t keyVariantCount = 8;

    AudioService(ConfigurationService& configuration, core::IAudioAdapter& adapter) noexcept
        : configuration_(configuration), adapter_(adapter) {}

    AudioResult start();
    bool suspend();
    bool resume();
    bool suspended() const noexcept { return suspended_; }
    AudioResult setVolume(std::uint8_t volumePercent);
    std::uint8_t volume() const noexcept { return configuration_.value().soundVolume; }
    bool play(AudioCue cue);
    core::ActionHandlingResult handle(const core::Action& action) override;

  private:
    ConfigurationService& configuration_;
    core::IAudioAdapter& adapter_;
    std::size_t nextKeyVariant_ = 0;
    bool started_ = false;
    bool suspended_ = false;
};

} // namespace cardputer_hub::services
