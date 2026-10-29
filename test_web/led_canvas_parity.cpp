// Read-only test bridge for comparing the real C++ engine with the inline JS model.
#include "apps/led_canvas/led_canvas_engine.h"
#include <iostream>
int main() {
    cardputer_hub::apps::LedCanvasEngine engine;
    unsigned command, mode;
    while (std::cin >> command >> mode) {
        engine.setSettings({static_cast<cardputer_hub::core::LedCanvasMode>(mode),
                            cardputer_hub::core::LedCanvasFade::Medium, 3, true});
        if (command == 0)
            engine.clear();
        else if (command == 1 || command == 3) {
            unsigned pointer, x, y, radius, flags, hue, ink;
            std::uint64_t time;
            std::cin >> pointer >> x >> y >> radius >> flags >> time >> hue >> ink;
            cardputer_hub::core::LedCanvasPlayback point{};
            point.pointer = static_cast<std::uint8_t>(pointer);
            point.time = time;
            point.point.x = static_cast<std::uint16_t>(x);
            point.point.y = static_cast<std::uint16_t>(y);
            point.point.radius = static_cast<std::uint8_t>(radius);
            point.point.flags = static_cast<std::uint8_t>(flags);
            point.point.hue = static_cast<std::uint16_t>(hue);
            point.point.mixInk = static_cast<std::uint8_t>(ink);
            point.point.hasColor = true;
            if (command == 3) {
                unsigned glow;
                std::cin >> glow;
                point.point.glow = glow != 0;
            }
            engine.point(point);
        } else if (command == 2) {
            unsigned elapsed;
            std::cin >> elapsed;
            engine.advance(std::chrono::milliseconds{elapsed});
        }
        for (const auto& pixel : engine.frame().pixels)
            std::cout << unsigned(pixel.red) << ' ' << unsigned(pixel.green) << ' '
                      << unsigned(pixel.blue) << ' ';
        std::cout << '\n';
    }
}
