/*
 * Speaker setup adapted from Codex Microputer ADV and modified for
 * Cardputer Hub's IAudioAdapter.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hardware/cardputer/cardputer_audio_adapter.h"

#include <M5Unified.hpp>

#include <iterator>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cardputer_hub::hardware {
namespace {

constexpr int interfaceChannel = 1;
constexpr std::uint8_t es8311Address = 0x18;
constexpr std::uint32_t codecI2cFrequency = 100000;
constexpr std::uint32_t speakerSampleRate = 48000;
constexpr TickType_t clockSettleDelay = pdMS_TO_TICKS(2);
constexpr TickType_t analogSettleDelay = pdMS_TO_TICKS(10);
constexpr TickType_t unmuteRampDelay = pdMS_TO_TICKS(40);

constexpr std::int16_t silentWarmup[256]{};

bool writeCodec(std::uint8_t reg, std::uint8_t value) {
    return M5.In_I2C.writeRegister8(es8311Address, reg, value, codecI2cFrequency);
}

void silenceCodec() {
    (void)writeCodec(0x31, 0x60); // Mute the DAC DSM and DEM paths.
    (void)writeCodec(0x32, 0x00); // Minimum digital volume.
    (void)writeCodec(0x12, 0x02); // Power down the DAC.
    (void)writeCodec(0x0D, 0xFC); // Power down analog references and VMID.
    (void)writeCodec(0x00, 0x1F); // Hold the digital blocks in reset.
}

bool startCodecMuted() {
    // The register values are the Cardputer-Adv M5Unified setup, with the DAC
    // muted before its analog path is enabled. Reg 0x37 temporarily enables a
    // fast soft ramp for the one startup unmute.
    return writeCodec(0x00, 0x80) && writeCodec(0x01, 0xB5) && writeCodec(0x02, 0x18) &&
           writeCodec(0x31, 0x60) && writeCodec(0x32, 0xBF) && writeCodec(0x37, 0x18) &&
           writeCodec(0x0D, 0x01) && writeCodec(0x12, 0x00) && writeCodec(0x13, 0x10);
}

std::uint8_t hardwareVolume(std::uint8_t volumePercent) {
    return static_cast<std::uint8_t>((static_cast<unsigned>(volumePercent) * 250U + 50U) / 100U);
}

} // namespace

bool CardputerAudioAdapter::begin(std::uint8_t volumePercent) {
    if (started_) {
        setVolume(volumePercent);
        return true;
    }
    auto config = M5.Speaker.config();
    config.pin_bck = GPIO_NUM_41;
    config.pin_ws = GPIO_NUM_43;
    config.pin_data_out = GPIO_NUM_42;
    config.i2s_port = I2S_NUM_1;
    config.magnification = 16;
    config.sample_rate = speakerSampleRate;
    config.dma_buf_len = 256;
    config.dma_buf_count = 16;
    config.task_priority = 3;
    config.task_pinned_core = 1;
    M5.Speaker.config(config);
    started_ = M5.Speaker.begin();
    if (!started_)
        return false;

    // Speaker_Class configures I2S in begin(), but starts the clocks only when
    // its worker receives data. Keep the codec off while a silent request starts
    // BCLK/LRCK; on this external-DAC path M5Unified leaves those clocks running.
    M5.Speaker.setVolume(0);
    if (!M5.Speaker.playRaw(silentWarmup, std::size(silentWarmup), speakerSampleRate, false, 1,
                            interfaceChannel, false)) {
        M5.Speaker.end();
        started_ = false;
        return false;
    }
    vTaskDelay(clockSettleDelay);
    if (!startCodecMuted()) {
        silenceCodec();
        M5.Speaker.end();
        started_ = false;
        return false;
    }
    vTaskDelay(analogSettleDelay);
    if (!writeCodec(0x31, 0x00)) {
        silenceCodec();
        M5.Speaker.end();
        started_ = false;
        return false;
    }
    vTaskDelay(unmuteRampDelay);
    if (!writeCodec(0x37, 0x08)) {
        silenceCodec();
        M5.Speaker.end();
        started_ = false;
        return false;
    }
    setVolume(volumePercent);
    return started_;
}

void CardputerAudioAdapter::end() {
    if (!started_)
        return;
    silenceCodec();
    M5.Speaker.end();
    started_ = false;
}

void CardputerAudioAdapter::setVolume(std::uint8_t volumePercent) {
    M5.Speaker.setVolume(hardwareVolume(volumePercent));
}

bool CardputerAudioAdapter::isPlaying() const {
    return started_ && M5.Speaker.isPlaying(interfaceChannel) != 0;
}

bool CardputerAudioAdapter::play(const core::AudioClip& clip) {
    if (!started_ || clip.samples == nullptr || clip.sampleCount == 0 || clip.sampleRate == 0)
        return false;
    return M5.Speaker.playRaw(clip.samples, clip.sampleCount, clip.sampleRate, false, 1,
                              interfaceChannel, false);
}

} // namespace cardputer_hub::hardware
