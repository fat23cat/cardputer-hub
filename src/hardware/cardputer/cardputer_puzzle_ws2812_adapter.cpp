#include "hardware/cardputer/cardputer_puzzle_ws2812_adapter.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "led_strip.h"

namespace cardputer_hub::hardware {
namespace {
led_strip_handle_t asStrip(void* strip) { return static_cast<led_strip_handle_t>(strip); }
} // namespace

EspPuzzleLedBackend::~EspPuzzleLedBackend() { close(); }

void EspPuzzleLedBackend::quietLine() {
    gpio_config_t pinConfig{};
    pinConfig.pin_bit_mask = 1ULL << puzzleLedGpio;
    pinConfig.mode = GPIO_MODE_OUTPUT;
    pinConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    pinConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
    pinConfig.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&pinConfig);
    gpio_set_level(static_cast<gpio_num_t>(puzzleLedGpio), 0);
}

void EspPuzzleLedBackend::close() {
    if (strip_ != nullptr) {
        auto* strip = asStrip(strip_);
        (void)led_strip_clear(strip);
        (void)led_strip_del(strip);
        strip_ = nullptr;
    }
    quietLine();
}

bool EspPuzzleLedBackend::open() {
    // Quiet the data line before RMT takes it. Unit Puzzle otherwise treats a
    // power-up edge as a random full-brightness frame.
    quietLine();
    esp_rom_delay_us(100);

    const led_strip_config_t stripConfig = {
        .strip_gpio_num = puzzleLedGpio,
        .max_leds = core::ledMatrixPixelCount,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags =
            {
                .invert_out = false,
            },
    };
    const led_strip_rmt_config_t rmtConfig = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags =
            {
                .with_dma = false,
            },
    };
    led_strip_handle_t strip = nullptr;
    if (led_strip_new_rmt_device(&stripConfig, &rmtConfig, &strip) != ESP_OK || strip == nullptr) {
        quietLine();
        return false;
    }
    if (led_strip_clear(strip) != ESP_OK) {
        (void)led_strip_del(strip);
        quietLine();
        return false;
    }
    strip_ = strip;
    return true;
}

bool EspPuzzleLedBackend::writeMappedFrame(const core::LedHardwareFrame& frame) {
    if (strip_ == nullptr)
        return false;
    auto* strip = asStrip(strip_);
    for (std::uint8_t y = 0; y < core::ledMatrixWidth; ++y) {
        for (std::uint8_t x = 0; x < core::ledMatrixWidth; ++x) {
            const auto logical = static_cast<std::uint8_t>(y * core::ledMatrixWidth + x);
            const auto color = frame.pixels[logical];
            if (led_strip_set_pixel(strip, puzzleWireIndex(x, y), color.red, color.green,
                                    color.blue) != ESP_OK) {
                return false;
            }
        }
    }
    return led_strip_refresh(strip) == ESP_OK;
}

} // namespace cardputer_hub::hardware
