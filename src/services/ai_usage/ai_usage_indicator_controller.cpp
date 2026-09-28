#include "services/ai_usage/ai_usage_indicator_controller.h"

#include "core/display/palette.h"

#include <algorithm>

namespace cardputer_hub::services {
namespace {
constexpr core::RgbColor boundaryPurple{0xA0, 0x50, 0xD0};

core::RgbColor colorFor(std::uint8_t percent) noexcept {
    if (percent >= 51)
        return core::palette::leaf;
    if (percent >= 20)
        return core::palette::blue;
    return core::palette::vermilion;
}
std::uint8_t lit(std::uint8_t percent, std::uint8_t capacity) noexcept {
    if (percent == 0)
        return 0;
    return std::max<std::uint8_t>(1, (capacity * percent + 50) / 100);
}
void fillHalf(IndicatorFrame& frame, const connectivity::AiUsageMetric& metric,
              bool bottom) noexcept {
    const auto begin = bottom ? 32 : 0;
    const auto end = begin + 31;
    frame.pixels[begin] = boundaryPurple;
    frame.pixels[end] = boundaryPurple;
    auto remaining = lit(metric.remainingPercent, 30);
    for (std::uint8_t i = begin; i < begin + 32 && remaining; ++i) {
        if (i == begin || i == end)
            continue;
        frame.pixels[i] = colorFor(metric.remainingPercent);
        --remaining;
    }
}
} // namespace

IndicatorFrame aiUsageGauge(const connectivity::AiUsageMetric* first,
                            const connectivity::AiUsageMetric* second) noexcept {
    IndicatorFrame frame{};
    if (first == nullptr)
        return frame;
    if (second != nullptr) {
        fillHalf(frame, *first, false);
        fillHalf(frame, *second, true);
    } else {
        const auto count = lit(first->remainingPercent, 64);
        for (std::uint8_t i = 0; i < count; ++i)
            frame.pixels[i] = colorFor(first->remainingPercent);
    }
    return frame;
}

std::array<const connectivity::AiUsageMetric*, 2>
AiUsageIndicatorController::overview() const noexcept {
    std::array<const connectivity::AiUsageMetric*, 2> result{};
    const auto& snapshot = usage_.snapshot();
    if (snapshot.providerCount == 0)
        return result;
    const auto& first = snapshot.providers[0];
    if (first.metricCount > 0)
        result[0] = &first.metrics[0];
    if (snapshot.providerCount > 1) {
        if (snapshot.providers[1].metricCount > 0)
            result[1] = &snapshot.providers[1].metrics[0];
    } else if (first.metricCount > 1)
        result[1] = &first.metrics[1];
    return result;
}

void AiUsageIndicatorController::focus(std::uint8_t index) {
    const auto metrics = overview();
    if (index >= metrics.size() || metrics[index] == nullptr)
        return;
    selected_ = index;
    focusRemaining_ = std::chrono::seconds(3);
    if (!focus_.valid())
        focus_ =
            indicator_.acquire(aiUsageIndicatorOwner, IndicatorPriority::ForegroundApplication);
    focus_.setFrame(aiUsageGauge(metrics[index]));
}

void AiUsageIndicatorController::clearFocus() {
    focus_.release();
    focusRemaining_ = {};
}

void AiUsageIndicatorController::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero())
        elapsed = {};
    if (usage_.session() != session_ || !usage_.available()) {
        session_ = usage_.session();
        background_.release();
        clearFocus();
        feedback_.release();
        feedbackRemaining_ = {};
        feedbackKind_ = Feedback::None;
        feedbackElapsed_ = {};
        previousValid_.fill(false);
        revision_ = 0;
    }
    if (focusRemaining_.count() > 0) {
        focusRemaining_ =
            elapsed >= focusRemaining_ ? std::chrono::milliseconds(0) : focusRemaining_ - elapsed;
        if (focusRemaining_.count() == 0)
            clearFocus();
    }
    if (feedbackRemaining_.count() > 0) {
        feedbackElapsed_ += elapsed;
        feedbackRemaining_ = elapsed >= feedbackRemaining_ ? std::chrono::milliseconds(0)
                                                           : feedbackRemaining_ - elapsed;
        if (feedbackRemaining_.count() == 0) {
            feedback_.release();
            feedbackKind_ = Feedback::None;
        } else if (feedbackKind_ == Feedback::Reset) {
            auto progress = feedbackMetric_;
            progress.remainingPercent = static_cast<std::uint8_t>(std::min<std::int64_t>(
                feedbackMetric_.remainingPercent,
                feedbackMetric_.remainingPercent * feedbackElapsed_.count() / 600));
            feedback_.setFrame(aiUsageGauge(&progress));
        } else {
            const auto segment = feedbackElapsed_.count() / 150;
            feedback_.setFrame(segment % 2 == 0 ? aiUsageGauge(&feedbackMetric_)
                                                : IndicatorFrame{});
        }
    }
    if (!usage_.available() || usage_.snapshot().providerCount == 0) {
        background_.release();
        return;
    }
    if (revision_ == usage_.revision())
        return;
    revision_ = usage_.revision();
    const auto metrics = overview();
    const auto& snapshot = usage_.snapshot();
    if (metrics[0] == nullptr)
        return;
    if (!background_.valid())
        background_ = indicator_.acquire(aiUsageIndicatorOwner, IndicatorPriority::Idle);
    background_.setFrame(aiUsageGauge(metrics[0], metrics[1]));
    if (focus_.valid())
        focus_.setFrame(aiUsageGauge(metrics[selected_]));
    for (std::uint8_t i = 0; i < 2; ++i) {
        if (metrics[i] == nullptr) {
            previousValid_[i] = false;
            continue;
        }
        const auto provider =
            snapshot.providers[i == 1 && snapshot.providerCount > 1 ? 1 : 0].provider;
        const auto kind = metrics[i]->kind;
        const auto percent = metrics[i]->remainingPercent;
        const auto reset = metrics[i]->resetAt;
        if (previousValid_[i] && previousProvider_[i] == provider && previousKind_[i] == kind) {
            const bool critical = previousPercent_[i] >= 5 && percent < 5;
            const bool low = previousPercent_[i] >= 20 && percent < 20;
            const bool resetCycle = previousReset_[i] != 0 && reset != 0 &&
                                    previousReset_[i] != reset && percent > previousPercent_[i];
            if (critical || low || resetCycle) {
                feedback_.release();
                feedback_ =
                    indicator_.acquire(aiUsageIndicatorOwner, critical ? IndicatorPriority::Warning
                                                                       : IndicatorPriority::Idle);
                feedbackMetric_ = *metrics[i];
                feedbackKind_ = resetCycle ? Feedback::Reset
                                : critical ? Feedback::Critical
                                           : Feedback::Low;
                feedbackElapsed_ = {};
                feedback_.setFrame(resetCycle ? IndicatorFrame{} : aiUsageGauge(metrics[i]));
                feedbackRemaining_ = resetCycle || critical ? std::chrono::milliseconds(600)
                                                            : std::chrono::milliseconds(300);
            }
        }
        previousPercent_[i] = percent;
        previousReset_[i] = reset;
        previousProvider_[i] = provider;
        previousKind_[i] = kind;
        previousValid_[i] = true;
    }
}

} // namespace cardputer_hub::services
