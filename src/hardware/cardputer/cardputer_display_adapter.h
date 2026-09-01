#pragma once

#include "core/display/display_adapter.h"

namespace cardputer_hub::hardware {

class CardputerDisplayAdapter final : public core::IDisplayAdapter {
  public:
    void clear(core::RgbColor color) override;
    void drawText(core::PixelPosition position, const char* text, core::TextStyle style) override;
};

} // namespace cardputer_hub::hardware
