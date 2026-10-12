#pragma once
#include <memory>

#include "core/display/display_adapter.h"

namespace cardputer_hub::hardware {

class CardputerDisplayAdapter final : public core::IDisplayAdapter {
  public:
    CardputerDisplayAdapter();
    ~CardputerDisplayAdapter() override;
    void beginFrame() override;
    void endFrame() override;
    bool beginOverlay() override;
    void endOverlay() override;
    void beginOverlayRegion(core::PixelRectangle region) override;
    void endOverlayRegion() override;
    void clear(core::RgbColor color) override;
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override;
    void drawText(core::PixelPosition position, const char* text, core::TextStyle style) override;

  private:
    struct Frame;
    struct Surface;
    Surface surface();
    std::unique_ptr<Frame> frame_;
};

} // namespace cardputer_hub::hardware
