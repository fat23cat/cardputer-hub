#pragma once

#include "core/display/display_adapter.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cardputer_hub::test_support {

// Records the same primitive stream consumed by scripts/render_ui_capture.py.
class UiCapture {
  public:
    void clear(core::RgbColor color) {
        commands_.clear();
        std::ostringstream line;
        line << "C " << unsigned(color.red) << ' ' << unsigned(color.green) << ' '
             << unsigned(color.blue);
        commands_.push_back(line.str());
    }

    void rectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                   core::RgbColor color) {
        std::ostringstream line;
        line << "R " << position.x << ' ' << position.y << ' ' << width << ' ' << height << ' '
             << unsigned(color.red) << ' ' << unsigned(color.green) << ' ' << unsigned(color.blue);
        commands_.push_back(line.str());
    }

    void text(core::PixelPosition position, const char* value, core::TextStyle style) {
        std::ostringstream line;
        line << "T " << position.x << ' ' << position.y << ' ' << std::fixed << std::setprecision(2)
             << static_cast<float>(style.scale) << ' ' << unsigned(style.foreground.red) << ' '
             << unsigned(style.foreground.green) << ' ' << unsigned(style.foreground.blue) << ' '
             << unsigned(style.background.red) << ' ' << unsigned(style.background.green) << ' '
             << unsigned(style.background.blue) << ' ' << std::quoted(value);
        commands_.push_back(line.str());
    }

    void save(const char* name) const {
        const auto* directory = std::getenv("CARDPUTER_UI_CAPTURE_DIR");
        if (directory == nullptr || commands_.empty())
            return;
        std::ofstream output(std::string(directory) + "/" + name + ".draw");
        for (const auto& command : commands_)
            output << command << '\n';
    }

  private:
    std::vector<std::string> commands_;
};

} // namespace cardputer_hub::test_support
