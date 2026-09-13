/*
 * Speaker setup adapted from Codex Microputer ADV and modified for
 * Cardputer Hub's IAudioAdapter.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hardware/cardputer/cardputer_audio_adapter.h"

#include <M5Unified.hpp>

namespace cardputer_hub::hardware {
namespace {

std::uint8_t hardwareVolume(std::uint8_t volumePercent) {
    return static_cast<std::uint8_t>((static_cast<unsigned>(volumePercent) * 250U + 50U) / 100U);
}

} // namespace

bool CardputerAudioAdapter::begin(std::uint8_t volumePercent) {
    if (started_) {
        setVolume(volumePercent);
        return true;
    }
    if (M5.Mic.isEnabled())
        M5.Mic.end();
    auto config = M5.Speaker.config();
    config.dma_buf_len = 256;
    config.dma_buf_count = 16;
    config.task_priority = 3;
    config.task_pinned_core = 1;
    M5.Speaker.config(config);
    started_ = M5.Speaker.begin();
    if (started_)
        setVolume(volumePercent);
    return started_;
}

void CardputerAudioAdapter::setVolume(std::uint8_t volumePercent) {
    M5.Speaker.setVolume(hardwareVolume(volumePercent));
}

bool CardputerAudioAdapter::play(const core::AudioClip& clip) {
    if (!started_ || clip.samples == nullptr || clip.sampleCount == 0 || clip.sampleRate == 0)
        return false;
    constexpr int interfaceChannel = 1;
    return M5.Speaker.playRaw(clip.samples, clip.sampleCount, clip.sampleRate, false, 1,
                              interfaceChannel, true);
}

} // namespace cardputer_hub::hardware
