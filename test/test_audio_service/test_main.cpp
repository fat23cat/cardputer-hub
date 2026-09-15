#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/actions/action_bus.h"
#include "core/audio/audio_adapter.h"
#include "core/storage/storage.h"
#include "services/audio/audio_service.h"
#include "services/configuration/configuration_service.h"

using namespace cardputer_hub;

namespace {
class MemoryStorage final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {readError       ? core::StorageReadStatus::BackendError
                : bytes.empty() ? core::StorageReadStatus::NotFound
                                : core::StorageReadStatus::Found,
                bytes};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes& value) override {
        ++writes;
        if (failWrites)
            return core::StorageWriteStatus::BackendError;
        bytes = value;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }

    core::StorageBytes bytes;
    bool readError = false;
    bool failWrites = false;
    int writes = 0;
};

class AudioAdapter final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t volumePercent) override {
        ++beginCalls;
        volumes.push_back(volumePercent);
        return beginResult;
    }
    void setVolume(std::uint8_t volumePercent) override { volumes.push_back(volumePercent); }
    bool isPlaying() const override { return playing; }
    bool play(const core::AudioClip& clip) override {
        clips.push_back(clip);
        return true;
    }

    bool beginResult = true;
    bool playing = false;
    int beginCalls = 0;
    std::vector<std::uint8_t> volumes;
    std::vector<core::AudioClip> clips;
};

struct Fixture {
    MemoryStorage memory;
    core::Storage storage{memory};
    services::ConfigurationService configuration{storage};
    AudioAdapter adapter;
    services::AudioService audio{configuration, adapter};
};

void test_defaults_to_sixty_percent_and_uses_a_bounded_precomputed_thock() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(60, fixture.configuration.value().soundVolume);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_EQUAL(1, fixture.adapter.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(60, fixture.adapter.volumes.back());

    TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::KeyPress));
    TEST_ASSERT_EQUAL_UINT(1, fixture.adapter.clips.size());
    const auto clip = fixture.adapter.clips.back();
    TEST_ASSERT_EQUAL_UINT32(16000, clip.sampleRate);
    TEST_ASSERT_EQUAL_UINT(1280, clip.sampleCount);
    TEST_ASSERT_NOT_NULL(clip.samples);
    TEST_ASSERT_TRUE(std::any_of(clip.samples, clip.samples + clip.sampleCount,
                                 [](std::int16_t sample) { return sample != 0; }));
}

void test_key_clicks_cycle_through_deterministic_variants_without_allocating_at_play_time() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    for (int index = 0; index < 9; ++index)
        TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::KeyPress));
    TEST_ASSERT_EQUAL_UINT(9, fixture.adapter.clips.size());
    TEST_ASSERT_NOT_EQUAL(fixture.adapter.clips[0].samples, fixture.adapter.clips[1].samples);
    TEST_ASSERT_EQUAL_PTR(fixture.adapter.clips[0].samples, fixture.adapter.clips[8].samples);
}

void test_every_key_click_variant_releases_to_digital_silence() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    for (int index = 0; index < 8; ++index) {
        TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::KeyPress));
        const auto clip = fixture.adapter.clips.back();
        TEST_ASSERT_TRUE(std::all_of(clip.samples + clip.sampleCount - 128,
                                     clip.samples + clip.sampleCount,
                                     [](std::int16_t sample) { return sample == 0; }));
    }
}

void test_active_interface_cue_is_not_interrupted_or_queued() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    fixture.adapter.playing = true;

    TEST_ASSERT_FALSE(fixture.audio.play(services::AudioCue::KeyPress));
    TEST_ASSERT_TRUE(fixture.adapter.clips.empty());

    fixture.adapter.playing = false;
    TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::KeyPress));
    TEST_ASSERT_EQUAL_UINT(1, fixture.adapter.clips.size());
}

void test_directional_volume_cues_release_to_digital_silence() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::StepLeft));
    TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::StepRight));
    for (const auto& clip : fixture.adapter.clips) {
        TEST_ASSERT_TRUE(std::all_of(clip.samples + clip.sampleCount - 128,
                                     clip.samples + clip.sampleCount,
                                     [](std::int16_t sample) { return sample == 0; }));
    }
}

void test_volume_is_persistent_in_ten_percent_steps_and_zero_mutes_playback() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.setVolume(80) == services::AudioResult::Success);
    TEST_ASSERT_EQUAL_UINT8(80, fixture.audio.volume());
    TEST_ASSERT_EQUAL_UINT8(80, fixture.adapter.volumes.back());

    services::ConfigurationService reloaded(fixture.storage);
    TEST_ASSERT_TRUE(reloaded.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(80, reloaded.value().soundVolume);

    TEST_ASSERT_TRUE(fixture.audio.setVolume(0) == services::AudioResult::Success);
    const auto played = fixture.adapter.clips.size();
    TEST_ASSERT_FALSE(fixture.audio.play(services::AudioCue::KeyPress));
    TEST_ASSERT_EQUAL_UINT(played, fixture.adapter.clips.size());
}

void test_volume_changes_preserve_wifi_configuration() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    auto value = fixture.configuration.value();
    value.wifi = {true, "Office", "recognizable-secret"};
    TEST_ASSERT_TRUE(fixture.configuration.save(value) == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);

    TEST_ASSERT_TRUE(fixture.audio.setVolume(80) == services::AudioResult::Success);

    services::ConfigurationService reloaded(fixture.storage);
    TEST_ASSERT_TRUE(reloaded.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reloaded.value().wifi.enabled);
    TEST_ASSERT_EQUAL_STRING("Office", reloaded.value().wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("recognizable-secret", reloaded.value().wifi.passphrase.c_str());
}

void test_invalid_or_unpersisted_volume_does_not_change_live_output() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.setVolume(55) == services::AudioResult::InvalidVolume);
    TEST_ASSERT_TRUE(fixture.audio.setVolume(110) == services::AudioResult::InvalidVolume);
    TEST_ASSERT_EQUAL_UINT8(60, fixture.audio.volume());
    fixture.memory.failWrites = true;
    TEST_ASSERT_TRUE(fixture.audio.setVolume(70) == services::AudioResult::StorageError);
    TEST_ASSERT_EQUAL_UINT8(60, fixture.audio.volume());
    TEST_ASSERT_EQUAL_UINT8(60, fixture.adapter.volumes.back());
}

void test_corrupt_configuration_blocks_audio_start_and_volume_writes() {
    Fixture fixture;
    fixture.memory.bytes = {255, 1, 2, 3};
    const auto corrupt = fixture.memory.bytes;

    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::StorageError);
    TEST_ASSERT_TRUE(fixture.audio.setVolume(70) == services::AudioResult::StorageError);
    TEST_ASSERT_EQUAL(0, fixture.memory.writes);
    TEST_ASSERT_TRUE(fixture.memory.bytes == corrupt);
    TEST_ASSERT_TRUE(fixture.adapter.volumes.empty());
}

void test_directional_volume_cues_are_distinct_and_unmute_uses_the_new_level() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::StepLeft));
    TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::StepRight));
    TEST_ASSERT_NOT_EQUAL(fixture.adapter.clips[0].samples, fixture.adapter.clips[1].samples);
    TEST_ASSERT_EQUAL_UINT(1760, fixture.adapter.clips[0].sampleCount);
    TEST_ASSERT_EQUAL_UINT(1760, fixture.adapter.clips[1].sampleCount);

    TEST_ASSERT_TRUE(fixture.audio.setVolume(0) == services::AudioResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.setVolume(10) == services::AudioResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.play(services::AudioCue::StepRight));
    TEST_ASSERT_EQUAL_UINT8(10, fixture.adapter.volumes.back());
}

void test_volume_step_action_owns_validation_clamping_and_persistence() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.configuration.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(fixture.audio.start() == services::AudioResult::Success);
    core::ActionBus actions;
    TEST_ASSERT_TRUE(actions.registerHandler("audio.volume.step", fixture.audio) ==
                     core::RegistrationResult::Registered);

    TEST_ASSERT_TRUE(
        actions.dispatch({"audio.volume.step", "settings", {{"delta", std::int32_t{10}}}}) ==
        core::DispatchResult::Handled);
    TEST_ASSERT_EQUAL_UINT8(70, fixture.audio.volume());
    TEST_ASSERT_TRUE(
        actions.dispatch({"audio.volume.step", "settings", {{"delta", std::int32_t{-10}}}}) ==
        core::DispatchResult::Handled);
    TEST_ASSERT_EQUAL_UINT8(60, fixture.audio.volume());

    TEST_ASSERT_TRUE(
        actions.dispatch({"audio.volume.step", "settings", {{"delta", std::int32_t{20}}}}) ==
        core::DispatchResult::Rejected);
    TEST_ASSERT_TRUE(
        actions.dispatch({"audio.volume.step", "settings", {{"delta", std::string{"10"}}}}) ==
        core::DispatchResult::Rejected);
    TEST_ASSERT_EQUAL_UINT8(60, fixture.audio.volume());

    TEST_ASSERT_TRUE(fixture.audio.setVolume(100) == services::AudioResult::Success);
    const auto volumeUpdates = fixture.adapter.volumes.size();
    TEST_ASSERT_TRUE(
        actions.dispatch({"audio.volume.step", "settings", {{"delta", std::int32_t{10}}}}) ==
        core::DispatchResult::Handled);
    TEST_ASSERT_EQUAL_UINT8(100, fixture.audio.volume());
    TEST_ASSERT_EQUAL_UINT(volumeUpdates, fixture.adapter.volumes.size());
}

void test_volume_step_recovers_from_a_failed_start_before_reading_the_current_volume() {
    MemoryStorage memory;
    core::Storage storage{memory};
    services::ConfigurationService writer{storage};
    TEST_ASSERT_TRUE(writer.load() == services::ConfigurationResult::Success);
    auto saved = writer.value();
    saved.soundVolume = 90;
    TEST_ASSERT_TRUE(writer.save(saved) == services::ConfigurationResult::Success);

    services::ConfigurationService recoveringConfiguration{storage};
    AudioAdapter adapter;
    services::AudioService audio{recoveringConfiguration, adapter};
    memory.readError = true;
    TEST_ASSERT_TRUE(audio.start() == services::AudioResult::StorageError);
    memory.readError = false;

    TEST_ASSERT_TRUE(
        audio.handle({"audio.volume.step", "settings", {{"delta", std::int32_t{10}}}}) ==
        core::ActionHandlingResult::Handled);
    TEST_ASSERT_EQUAL_UINT8(100, audio.volume());
    TEST_ASSERT_EQUAL_UINT8(100, adapter.volumes.back());
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_to_sixty_percent_and_uses_a_bounded_precomputed_thock);
    RUN_TEST(test_key_clicks_cycle_through_deterministic_variants_without_allocating_at_play_time);
    RUN_TEST(test_every_key_click_variant_releases_to_digital_silence);
    RUN_TEST(test_active_interface_cue_is_not_interrupted_or_queued);
    RUN_TEST(test_directional_volume_cues_release_to_digital_silence);
    RUN_TEST(test_volume_is_persistent_in_ten_percent_steps_and_zero_mutes_playback);
    RUN_TEST(test_volume_changes_preserve_wifi_configuration);
    RUN_TEST(test_invalid_or_unpersisted_volume_does_not_change_live_output);
    RUN_TEST(test_corrupt_configuration_blocks_audio_start_and_volume_writes);
    RUN_TEST(test_directional_volume_cues_are_distinct_and_unmute_uses_the_new_level);
    RUN_TEST(test_volume_step_action_owns_validation_clamping_and_persistence);
    RUN_TEST(test_volume_step_recovers_from_a_failed_start_before_reading_the_current_volume);
    return UNITY_END();
}
