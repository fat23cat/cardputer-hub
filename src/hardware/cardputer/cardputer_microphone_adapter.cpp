#include "hardware/cardputer/cardputer_microphone_adapter.h"

#include <M5Unified.hpp>

#include <cstdint>

#include "driver/i2s_std.h"

namespace cardputer_hub::hardware {
namespace {
constexpr std::uint8_t es8311Address = 0x18;
constexpr std::uint32_t codecI2cFrequency = 100000;

bool writeCodec(std::uint8_t reg, std::uint8_t value) {
    return M5.In_I2C.writeRegister8(es8311Address, reg, value, codecI2cFrequency);
}

bool enableCodec() {
    // Match the working Cardputer-Adv ES8311 microphone setup: internal clock
    // from BCLK and 30 dB analog PGA gain for the built-in MEMS microphone.
    // https://github.com/lfurze/cardputer-voice-assistant/blob/main/device/term.py
    return writeCodec(0x00, 0x80) && writeCodec(0x01, 0xBA) && writeCodec(0x02, 0x18) &&
           writeCodec(0x0D, 0x01) && writeCodec(0x0E, 0x02) && writeCodec(0x14, 0x1A) &&
           writeCodec(0x17, 0xBF) && writeCodec(0x1C, 0x6A);
}

void disableCodec() {
    (void)writeCodec(0x0D, 0xFC);
    (void)writeCodec(0x0E, 0x6A);
    (void)writeCodec(0x00, 0x00);
}

} // namespace

bool CardputerMicrophoneAdapter::begin(std::uint32_t sampleRate) {
    if (channel_ != nullptr)
        return true;
    if (sampleRate == 0)
        return false;

    if (!enableCodec()) {
        disableCodec();
        return false;
    }

    const i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&channelConfig, nullptr, &channel_) != ESP_OK) {
        channel_ = nullptr;
        disableCodec();
        return false;
    }

    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
        // MicroPython's I2S.RX uses 32-bit stereo DMA even when its public
        // format is 16-bit mono, then selects the high 16 bits of one slot.
        // https://github.com/micropython/micropython/blob/master/ports/esp32/machine_i2s.c
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = GPIO_NUM_NC,
                .bclk = GPIO_NUM_41,
                .ws = GPIO_NUM_43,
                .dout = GPIO_NUM_NC,
                .din = GPIO_NUM_46,
                .invert_flags = {},
            },
    };
    const bool ready = i2s_channel_init_std_mode(channel_, &config) == ESP_OK &&
                       i2s_channel_enable(channel_) == ESP_OK;
    if (!ready) {
        end();
        return false;
    }
    bufferedWords_ = 0;
    return true;
}

void CardputerMicrophoneAdapter::end() {
    if (channel_ == nullptr)
        return;
    disableCodec();
    (void)i2s_channel_disable(channel_);
    (void)i2s_del_channel(channel_);
    channel_ = nullptr;
    bufferedWords_ = 0;
}

core::MicrophoneReadResult CardputerMicrophoneAdapter::read(std::int16_t* samples,
                                                            std::size_t sampleCount) {
    if (channel_ == nullptr || samples == nullptr || sampleCount != windowSamples)
        return core::MicrophoneReadResult::Error;

    std::size_t bytesRead = 0;
    const auto remainingBytes = (stereoWords - bufferedWords_) * sizeof(std::int32_t);
    const esp_err_t result =
        i2s_channel_read(channel_, buffer_.data() + bufferedWords_, remainingBytes, &bytesRead, 1);
    if (result != ESP_OK && result != ESP_ERR_TIMEOUT)
        return core::MicrophoneReadResult::Error;
    if (bytesRead > remainingBytes || bytesRead % (2 * sizeof(std::int32_t)) != 0)
        return core::MicrophoneReadResult::Error;
    bufferedWords_ += bytesRead / sizeof(std::int32_t);
    if (bufferedWords_ < stereoWords)
        return core::MicrophoneReadResult::Pending;

    for (std::size_t i = 0; i < windowSamples; ++i)
        samples[i] = static_cast<std::int16_t>(static_cast<std::uint32_t>(buffer_[i * 2]) >> 16);
    bufferedWords_ = 0;
    return core::MicrophoneReadResult::Ready;
}

} // namespace cardputer_hub::hardware
