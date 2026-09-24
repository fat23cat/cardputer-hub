#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace cardputer_hub::apps {

// Critically damped motion for the Home action focus plate.
class FocusPlateMotion {
  public:
    void reset(float position) {
        position_ = position;
        target_ = position;
        velocity_ = 0;
    }

    void setTarget(float target) { target_ = target; }
    float position() const { return position_; }
    bool moving() const {
        return std::fabs(position_ - target_) > settleThreshold ||
               std::fabs(velocity_) > settleThreshold;
    }

    void advance(std::chrono::milliseconds elapsed) {
        const auto seconds =
            static_cast<float>(std::max<std::int64_t>(0, elapsed.count())) / 1000.0f;
        if (seconds <= 0)
            return;
        const float x = position_ - target_;
        if (!moving()) {
            position_ = target_;
            velocity_ = 0;
            return;
        }
        const float decay = std::exp(springOmega * -seconds);
        const float combo = velocity_ + springOmega * x;
        position_ = target_ + (x + combo * seconds) * decay;
        velocity_ = (velocity_ - springOmega * combo * seconds) * decay;
        if (!moving()) {
            position_ = target_;
            velocity_ = 0;
        }
    }

  private:
    static constexpr float springOmega = 32.0f;
    static constexpr float settleThreshold = 0.01f;
    float position_ = 0;
    float velocity_ = 0;
    float target_ = 0;
};

} // namespace cardputer_hub::apps
