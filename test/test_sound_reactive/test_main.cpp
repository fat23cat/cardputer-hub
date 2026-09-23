#include <unity.h>

#include "apps/sound_reactive/sound_reactive_app.h"
#include "apps/sound_reactive/sound_reactive_graphics.h"
#include "core/storage/storage.h"
#include "services/configuration/configuration_service.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace cardputer_hub;

namespace {
class MemoryStorage final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {core::StorageReadStatus::NotFound, {}};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes&) override {
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
};

class Speaker final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t volume) override {
        ++starts;
        lastVolume = volume;
        if (sequence != nullptr)
            lastBeginOrder = ++*sequence;
        return startSucceeds;
    }
    void end() override { ++stops; }
    void setVolume(std::uint8_t) override {}
    bool isPlaying() const override { return false; }
    bool play(const core::AudioClip&) override {
        ++plays;
        return true;
    }
    bool startSucceeds = true;
    int starts = 0;
    int stops = 0;
    int plays = 0;
    std::uint8_t lastVolume = 0;
    int* sequence = nullptr;
    int lastBeginOrder = 0;
};

class Microphone final : public core::IMicrophoneAdapter {
  public:
    bool begin(std::uint32_t rate) override {
        ++starts;
        lastRate = rate;
        return startSucceeds;
    }
    void end() override {
        ++stops;
        if (sequence != nullptr)
            lastEndOrder = ++*sequence;
    }
    core::MicrophoneReadResult read(std::int16_t* samples, std::size_t count) override {
        if (!ready)
            return core::MicrophoneReadResult::Pending;
        ready = false;
        if (rearmFails)
            return core::MicrophoneReadResult::Error;
        for (std::size_t i = 0; i < count; ++i)
            samples[i] = i % 2 ? amplitude : static_cast<std::int16_t>(-amplitude);
        return core::MicrophoneReadResult::Ready;
    }
    bool startSucceeds = true;
    bool ready = false;
    bool rearmFails = false;
    std::int16_t amplitude = 0;
    int starts = 0;
    int stops = 0;
    std::uint32_t lastRate = 0;
    int* sequence = nullptr;
    int lastEndOrder = 0;
};

class Led final : public core::ILEDAdapter {
  public:
    void writeFrame(const core::LedHardwareFrame&) override { ++writes; }
    int writes = 0;
};

class Display final : public core::IDisplayAdapter {
  public:
    void clear(core::RgbColor) override { ++clears; }
    void fillRectangle(core::PixelPosition, std::int32_t, std::int32_t, core::RgbColor) override {
        ++rectangles;
    }
    void drawText(core::PixelPosition, const char* text, core::TextStyle) override {
        ++texts;
        drawnText.emplace_back(text);
    }
    int clears = 0;
    int rectangles = 0;
    int texts = 0;
    std::vector<std::string> drawnText;
};

struct Fixture {
    Fixture() {
        speaker.sequence = &sequence;
        microphone.sequence = &sequence;
    }
    int sequence = 0;
    MemoryStorage memory;
    core::Storage storage{memory};
    services::ConfigurationService configuration{storage};
    Speaker speaker;
    services::AudioService audio{configuration, speaker};
    Microphone microphone;
    services::MicrophoneService service{microphone, audio};
    Led led;
    services::IndicatorService indicator{led};
    Display display;
    apps::SoundReactiveApp app{service, indicator, display};
};

void test_pcm_analysis_removes_dc_and_is_bounded() {
    std::array<std::int16_t, 256> samples{};
    TEST_ASSERT_EQUAL_FLOAT(
        -96.0f, services::MicrophoneService::windowDbfs(samples.data(), samples.size()));
    samples.fill(1000);
    TEST_ASSERT_EQUAL_FLOAT(
        -96.0f, services::MicrophoneService::windowDbfs(samples.data(), samples.size()));
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = i % 2 ? 16000 : -16000;
    TEST_ASSERT_TRUE(services::MicrophoneService::windowDbfs(samples.data(), samples.size()) >
                     -7.0f);
}

void test_loud_window_attacks_and_quiet_window_releases() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_TRUE(f.service.start());
    TEST_ASSERT_EQUAL_UINT32(16000, f.microphone.lastRate);
    f.microphone.amplitude = 30000;
    f.microphone.ready = true;
    f.service.update(std::chrono::milliseconds{100});
    const auto loud = f.service.snapshot().normalizedLevel;
    TEST_ASSERT_TRUE(loud > 0.5f);
    TEST_ASSERT_TRUE(loud <= 1.0f);
    f.microphone.amplitude = 0;
    f.microphone.ready = true;
    f.service.update(std::chrono::milliseconds{100});
    const auto released = f.service.snapshot().normalizedLevel;
    TEST_ASSERT_TRUE(released < loud);
    TEST_ASSERT_TRUE(released > loud * 0.7f);
    f.service.stop();
    TEST_ASSERT_EQUAL(1, f.microphone.stops);
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
}

void test_saturated_input_stays_at_high_relative_level() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_TRUE(f.service.start());
    f.microphone.amplitude = 32767;
    for (int i = 0; i < 30; ++i) {
        f.microphone.ready = true;
        f.service.update(std::chrono::milliseconds{1000});
    }
    TEST_ASSERT_TRUE(f.service.snapshot().normalizedLevel > 0.95f);
    TEST_ASSERT_TRUE(f.service.snapshot().ambientLevel <= 0.70f);
    f.service.stop();
}

void test_capture_diagnostics_distinguish_pending_from_completed_pcm() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    TEST_ASSERT_TRUE(f.service.start());
    f.service.update(std::chrono::milliseconds{20});
    TEST_ASSERT_EQUAL_UINT32(0, f.service.snapshot().windowCount);
    f.microphone.amplitude = 1200;
    f.microphone.ready = true;
    f.service.update(std::chrono::milliseconds{20});
    const auto snapshot = f.service.snapshot();
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.windowCount);
    TEST_ASSERT_EQUAL_INT16(-1200, snapshot.sampleMin);
    TEST_ASSERT_EQUAL_INT16(1200, snapshot.sampleMax);
    TEST_ASSERT_TRUE(snapshot.dbfs > -30.0f);
    f.service.stop();
    TEST_ASSERT_EQUAL_UINT32(0, f.service.snapshot().windowCount);
}

void test_d_toggles_capture_diagnostics_on_screen() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    const core::InputEvent d{
        core::InputEventType::PrintableCharacter, 'd', core::NamedKey::Count, {}};
    f.app.update({d}, std::chrono::milliseconds{40});
    TEST_ASSERT_EQUAL(4, f.display.texts);
    TEST_ASSERT_EQUAL_STRING("WINDOWS 0", f.display.drawnText.front().c_str());
    f.microphone.amplitude = 1200;
    f.microphone.ready = true;
    f.service.update(std::chrono::milliseconds{20});
    f.app.update({}, std::chrono::milliseconds{40});
    TEST_ASSERT_EQUAL_STRING("WINDOWS 1", f.display.drawnText[4].c_str());
    TEST_ASSERT_EQUAL_STRING("PCM -1200..1200", f.display.drawnText[5].c_str());
    f.app.update({d}, std::chrono::milliseconds{40});
    TEST_ASSERT_EQUAL(8, f.display.texts);
    f.app.onDeactivate();
}

void test_start_failure_restores_speaker_and_app_shows_error() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.microphone.startSucceeds = false;
    f.app.onActivate();
    TEST_ASSERT_TRUE(f.service.snapshot().state == services::MicrophoneState::Failed);
    TEST_ASSERT_EQUAL(1, f.speaker.stops);
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
    f.app.update({}, std::chrono::milliseconds{40});
    TEST_ASSERT_EQUAL(1, f.display.texts);
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
    f.app.onDeactivate();
}

void test_pending_window_preserves_capture_and_speaker_suspension() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    f.service.update(std::chrono::milliseconds{10});
    TEST_ASSERT_TRUE(f.service.snapshot().state == services::MicrophoneState::Capturing);
    TEST_ASSERT_TRUE(f.audio.suspended());
    TEST_ASSERT_EQUAL(1, f.speaker.starts);
    TEST_ASSERT_EQUAL(0, f.microphone.stops);
    f.app.onDeactivate();
}

void test_stop_during_active_window_joins_microphone_before_speaker_restart() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    TEST_ASSERT_TRUE(f.audio.suspended());
    // The fake still has a pending window; shutdown must not require a read.
    f.app.onDeactivate();
    TEST_ASSERT_EQUAL(1, f.microphone.stops);
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
    TEST_ASSERT_TRUE(f.microphone.lastEndOrder < f.speaker.lastBeginOrder);
    f.service.stop();
    TEST_ASSERT_EQUAL(1, f.microphone.stops);
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
}

void test_speaker_restart_failure_after_stop_leaves_audio_unavailable() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    f.speaker.startSucceeds = false;
    f.app.onDeactivate();
    TEST_ASSERT_EQUAL(1, f.microphone.stops);
    TEST_ASSERT_TRUE(f.microphone.lastEndOrder < f.speaker.lastBeginOrder);
    TEST_ASSERT_FALSE(f.audio.suspended());
    TEST_ASSERT_FALSE(f.audio.play(services::AudioCue::KeyPress));
    f.service.stop();
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
    f.speaker.startSucceeds = true;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
}

void test_rearm_failure_stops_capture_restores_speaker_and_shows_error() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    f.microphone.amplitude = 30000;
    f.microphone.ready = true;
    f.service.update(std::chrono::milliseconds{100});
    TEST_ASSERT_TRUE(f.service.snapshot().normalizedLevel > 0.5f);
    f.app.update({}, std::chrono::milliseconds{40});
    f.indicator.update();
    TEST_ASSERT_TRUE(f.indicator.resolved().hasFrame);

    f.microphone.rearmFails = true;
    f.microphone.ready = true;
    f.service.update(std::chrono::milliseconds{20});
    TEST_ASSERT_TRUE(f.service.snapshot().state == services::MicrophoneState::Failed);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.service.snapshot().normalizedLevel);
    TEST_ASSERT_EQUAL(1, f.microphone.stops);
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
    TEST_ASSERT_FALSE(f.audio.suspended());
    f.app.update({}, std::chrono::milliseconds{40});
    f.indicator.update();
    TEST_ASSERT_EQUAL(1, f.display.texts);
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);

    f.app.onDeactivate();
    f.app.onDeactivate();
    TEST_ASSERT_EQUAL(1, f.microphone.stops);
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
    f.microphone.rearmFails = false;
    f.app.onActivate();
    TEST_ASSERT_TRUE(f.service.snapshot().state == services::MicrophoneState::Capturing);
    f.app.onDeactivate();
    TEST_ASSERT_EQUAL(2, f.microphone.stops);
    TEST_ASSERT_EQUAL(3, f.speaker.starts);
}

void test_speaker_restart_failure_after_capture_error_is_known_and_retryable() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    f.speaker.startSucceeds = false;
    f.microphone.rearmFails = true;
    f.microphone.ready = true;
    f.service.update(std::chrono::milliseconds{20});
    TEST_ASSERT_TRUE(f.service.snapshot().state == services::MicrophoneState::Failed);
    TEST_ASSERT_FALSE(f.audio.suspended());
    TEST_ASSERT_FALSE(f.audio.play(services::AudioCue::KeyPress));
    f.app.onDeactivate();
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
    f.speaker.startSucceeds = true;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
}

void test_app_owns_foreground_claim_and_elapsed_motion() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    f.app.update({}, std::chrono::milliseconds{1000});
    TEST_ASSERT_TRUE(std::abs(f.app.phase() - 0.15f) < 0.01f);
    TEST_ASSERT_TRUE(f.display.rectangles > 0);
    f.indicator.update();
    TEST_ASSERT_EQUAL_STRING(services::soundReactiveIndicatorOwner,
                             f.indicator.resolved().owner.c_str());
    TEST_ASSERT_EQUAL_UINT8(3, f.indicator.resolved().brightnessPercent);
    f.app.onDeactivate();
    f.indicator.update();
    TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
    TEST_ASSERT_EQUAL(1, f.microphone.stops);
    TEST_ASSERT_EQUAL(2, f.speaker.starts);
}

void test_color_and_speed_are_continuous() {
    const auto green = apps::soundReactiveColor(0);
    const auto yellow = apps::soundReactiveColor(0.5f);
    const auto red = apps::soundReactiveColor(1);
    TEST_ASSERT_EQUAL_UINT8(0, green.red);
    TEST_ASSERT_EQUAL_UINT8(255, green.green);
    TEST_ASSERT_EQUAL_UINT8(255, yellow.red);
    TEST_ASSERT_EQUAL_UINT8(220, yellow.green);
    TEST_ASSERT_EQUAL_UINT8(255, red.red);
    TEST_ASSERT_EQUAL_UINT8(0, red.green);
    TEST_ASSERT_TRUE(apps::soundReactiveSpeed(0) < apps::soundReactiveSpeed(0.5f));
    TEST_ASSERT_TRUE(apps::soundReactiveSpeed(0.5f) < apps::soundReactiveSpeed(1));
}

void test_repeated_sessions_release_resources_and_suppress_cues() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    for (int i = 0; i < 10; ++i) {
        f.app.onActivate();
        TEST_ASSERT_FALSE(f.audio.play(services::AudioCue::KeyPress));
        f.app.update({}, std::chrono::milliseconds{60});
        f.indicator.update();
        TEST_ASSERT_EQUAL_STRING(services::soundReactiveIndicatorOwner,
                                 f.indicator.resolved().owner.c_str());
        f.app.onDeactivate();
        f.indicator.update();
        TEST_ASSERT_FALSE(f.indicator.resolved().hasFrame);
        TEST_ASSERT_FALSE(f.audio.suspended());
    }
    TEST_ASSERT_EQUAL(10, f.microphone.starts);
    TEST_ASSERT_EQUAL(10, f.microphone.stops);
    TEST_ASSERT_EQUAL(10, f.speaker.stops);
    TEST_ASSERT_EQUAL(11, f.speaker.starts);
}

void test_large_elapsed_keeps_phase_bounded_without_catch_up_frames() {
    Fixture f;
    TEST_ASSERT_TRUE(f.audio.start() == services::AudioResult::Success);
    f.app.onActivate();
    f.app.update({}, std::chrono::milliseconds{9223372036854775807LL});
    TEST_ASSERT_TRUE(std::isfinite(f.app.phase()));
    TEST_ASSERT_TRUE(f.app.phase() >= 0.0f && f.app.phase() < 1.0f);
    TEST_ASSERT_EQUAL(1, f.display.clears);
    f.app.onDeactivate();
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_pcm_analysis_removes_dc_and_is_bounded);
    RUN_TEST(test_loud_window_attacks_and_quiet_window_releases);
    RUN_TEST(test_saturated_input_stays_at_high_relative_level);
    RUN_TEST(test_capture_diagnostics_distinguish_pending_from_completed_pcm);
    RUN_TEST(test_d_toggles_capture_diagnostics_on_screen);
    RUN_TEST(test_start_failure_restores_speaker_and_app_shows_error);
    RUN_TEST(test_pending_window_preserves_capture_and_speaker_suspension);
    RUN_TEST(test_stop_during_active_window_joins_microphone_before_speaker_restart);
    RUN_TEST(test_speaker_restart_failure_after_stop_leaves_audio_unavailable);
    RUN_TEST(test_rearm_failure_stops_capture_restores_speaker_and_shows_error);
    RUN_TEST(test_speaker_restart_failure_after_capture_error_is_known_and_retryable);
    RUN_TEST(test_app_owns_foreground_claim_and_elapsed_motion);
    RUN_TEST(test_color_and_speed_are_continuous);
    RUN_TEST(test_repeated_sessions_release_resources_and_suppress_cues);
    RUN_TEST(test_large_elapsed_keeps_phase_bounded_without_catch_up_frames);
    return UNITY_END();
}
