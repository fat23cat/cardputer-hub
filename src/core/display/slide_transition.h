#pragma once
#include <chrono>
#include <cstdint>
namespace cardputer_hub::core {
enum class SlideDirection { Forward, Backward };
class SlideTransition {
  public:
    void start(SlideDirection direction);
    void advance(std::chrono::milliseconds elapsed);
    bool active() const { return elapsed_ < 220; }
    int offset() const;
    SlideDirection direction() const { return direction_; }

  private:
    int elapsed_ = 220;
    SlideDirection direction_ = SlideDirection::Forward;
};
// Replace outgoing with the visible intermediate frame, in place. Both buffers
// have identical pixel format. This lets a new input interrupt a slide smoothly.
void composeSlideSnapshot(std::uint16_t* outgoing, const std::uint16_t* incoming, int width,
                          int height, int offset, SlideDirection direction);
} // namespace cardputer_hub::core
