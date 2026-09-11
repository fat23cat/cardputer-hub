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
    void advanceTransition(std::chrono::milliseconds elapsed) override;
    void beginTransition(core::SlideDirection direction) override;
    bool transitionActive() const override;
    void clear(core::RgbColor color) override;
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override;
    void drawText(core::PixelPosition position, const char* text, core::TextStyle style) override;

  private:
    struct Frame;
    std::unique_ptr<Frame> frame_;
};

} // namespace cardputer_hub::hardware
