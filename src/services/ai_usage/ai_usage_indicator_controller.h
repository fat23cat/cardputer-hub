#pragma once

#include "services/ai_usage/ai_usage_service.h"
#include "services/indicator/indicator_service.h"

#include <array>
#include <chrono>

namespace cardputer_hub::services {

inline constexpr char aiUsageIndicatorOwner[] = "ai-usage";

IndicatorFrame aiUsageGauge(const connectivity::AiUsageMetric* first,
                            const connectivity::AiUsageMetric* second = nullptr) noexcept;

class AiUsageIndicatorController {
  public:
    AiUsageIndicatorController(AiUsageService& usage, IndicatorService& indicator)
        : usage_(usage), indicator_(indicator) {}
    void update(std::chrono::milliseconds elapsed);
    void focus(std::uint8_t index);
    void clearFocus();
    bool focused() const noexcept { return focus_.valid(); }

  private:
    std::array<const connectivity::AiUsageMetric*, 2> overview() const noexcept;
    AiUsageService& usage_;
    IndicatorService& indicator_;
    IndicatorClaim background_;
    IndicatorClaim focus_;
    IndicatorClaim feedback_;
    std::uint16_t session_ = 0;
    std::uint32_t revision_ = 0;
    std::uint8_t selected_ = 0;
    std::chrono::milliseconds focusRemaining_{0};
    std::chrono::milliseconds feedbackRemaining_{0};
    std::array<std::uint8_t, 2> previousPercent_{};
    std::array<std::uint32_t, 2> previousReset_{};
    std::array<connectivity::AiProvider, 2> previousProvider_{};
    std::array<connectivity::AiMetricKind, 2> previousKind_{};
    std::array<bool, 2> previousValid_{};
    enum class Feedback : std::uint8_t {
        None,
        Low,
        Critical,
        Reset
    } feedbackKind_ = Feedback::None;
    connectivity::AiUsageMetric feedbackMetric_{};
    std::chrono::milliseconds feedbackElapsed_{0};
};

} // namespace cardputer_hub::services
