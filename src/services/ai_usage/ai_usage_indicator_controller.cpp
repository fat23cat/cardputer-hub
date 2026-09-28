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
// Purple pixels mark both ends of a band and are not quota.
void fillBand(IndicatorFrame& frame, const connectivity::AiUsageMetric& metric, std::uint8_t begin,
              std::uint8_t size) noexcept {
    const auto end = static_cast<std::uint8_t>(begin + size - 1);
    frame.pixels[begin] = boundaryPurple;
    frame.pixels[end] = boundaryPurple;
    auto remaining = lit(metric.remainingPercent, static_cast<std::uint8_t>(size - 2));
    for (std::uint8_t i = begin + 1; i < end && remaining; ++i, --remaining)
        frame.pixels[i] = colorFor(metric.remainingPercent);
}
} // namespace

IndicatorFrame aiUsageGauge(const AiUsageGaugeMetrics& metrics) noexcept {
    IndicatorFrame frame{};
    std::uint8_t count = 0;
    while (count < metrics.size() && metrics[count] != nullptr)
        ++count;
    if (count == 1) {
        const auto pixels = lit(metrics[0]->remainingPercent, 64);
        for (std::uint8_t i = 0; i < pixels; ++i)
            frame.pixels[i] = colorFor(metrics[0]->remainingPercent);
    } else if (count > 1) {
        const std::uint8_t size = count == 2 ? 32 : 16;
        for (std::uint8_t i = 0; i < count; ++i)
            fillBand(frame, *metrics[i], static_cast<std::uint8_t>(i * size), size);
    }
    return frame;
}

IndicatorFrame aiUsageGauge(const connectivity::AiUsageMetric* first,
                            const connectivity::AiUsageMetric* second) noexcept {
    return aiUsageGauge(AiUsageGaugeMetrics{first, first != nullptr ? second : nullptr});
}

AiUsageGaugeMetrics AiUsageIndicatorController::overview() const noexcept {
    AiUsageGaugeMetrics result{};
    const auto& snapshot = usage_.snapshot();
    const auto visible = aiUsageVisibleMetrics(snapshot);
    for (std::uint8_t i = 0; i < visible.count; ++i)
        result[i] = &snapshot.providers[visible.items[i].provider].metrics[visible.items[i].metric];
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
    background_.setFrame(aiUsageGauge(metrics));
    if (focus_.valid())
        focus_.setFrame(aiUsageGauge(metrics[selected_]));
    const auto visible = aiUsageVisibleMetrics(snapshot);
    for (std::uint8_t i = 0; i < metrics.size(); ++i) {
        if (metrics[i] == nullptr) {
            previousValid_[i] = false;
            continue;
        }
        const auto provider = snapshot.providers[visible.items[i].provider].provider;
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
