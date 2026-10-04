#include "hardware/esp32/esp32_random_source.h"

#include <esp_random.h>

namespace cardputer_hub::hardware {

void Esp32RandomSource::fill(std::uint8_t* destination, std::size_t size) {
    esp_fill_random(destination, size);
}

} // namespace cardputer_hub::hardware
