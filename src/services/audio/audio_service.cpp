#include "services/audio/audio_service.h"

#include <algorithm>

#include "services/audio/assets/interface_clips.h"

namespace cardputer_hub::services {

AudioResult AudioService::start() {
    if (started_)
        return AudioResult::Success;
    if (configuration_.ensureLoaded() != ConfigurationResult::Success)
        return AudioResult::StorageError;
    if (!adapter_.begin(volume()))
        return AudioResult::AdapterError;
    started_ = true;
    return AudioResult::Success;
}

AudioResult AudioService::setVolume(std::uint8_t volumePercent) {
    if (volumePercent > 100 || volumePercent % 10 != 0)
        return AudioResult::InvalidVolume;
    if (configuration_.ensureLoaded() != ConfigurationResult::Success)
        return AudioResult::StorageError;
    if (volumePercent == volume())
        return AudioResult::Success;
    auto next = configuration_.value();
    next.soundVolume = volumePercent;
    if (configuration_.save(next) != ConfigurationResult::Success)
        return AudioResult::StorageError;
    if (started_)
        adapter_.setVolume(volumePercent);
    return AudioResult::Success;
}

bool AudioService::play(AudioCue cue) {
    if (!started_ || volume() == 0 || adapter_.isPlaying())
        return false;
    if (cue == AudioCue::KeyPress) {
        const auto index = nextKeyVariant_;
        nextKeyVariant_ = (nextKeyVariant_ + 1) % keyVariantCount;
        return adapter_.play({audio_assets::keyClips[index], keyClipLength, sampleRate});
    }
    const auto index = cue == AudioCue::StepLeft ? 0U : 1U;
    return adapter_.play({audio_assets::stepClips[index], stepClipLength, sampleRate});
}

core::ActionHandlingResult AudioService::handle(const core::Action& action) {
    if (action.id != "audio.volume.step")
        return core::ActionHandlingResult::Rejected;
    const auto* value = action.findParameter("delta");
    const auto* delta = value ? std::get_if<std::int32_t>(value) : nullptr;
    if (!delta || (*delta != -10 && *delta != 10))
        return core::ActionHandlingResult::Rejected;
    if (configuration_.ensureLoaded() != ConfigurationResult::Success)
        return core::ActionHandlingResult::Rejected;
    // A settings action is also the explicit retry path after a transient
    // adapter-start failure. Persistence remains usable if the retry fails.
    if (!started_)
        (void)start();
    const auto next = static_cast<std::uint8_t>(std::clamp(
        static_cast<std::int32_t>(volume()) + *delta, std::int32_t{0}, std::int32_t{100}));
    return setVolume(next) == AudioResult::Success ? core::ActionHandlingResult::Handled
                                                   : core::ActionHandlingResult::Rejected;
}

} // namespace cardputer_hub::services
