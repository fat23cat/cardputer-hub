#include "apps/ai_usage/ai_usage_app.h"

#include "core/display/palette.h"
#include "core/display/text_layout.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cardputer_hub::apps {
namespace {
using namespace core;
const char* planName(connectivity::AiPlan plan) {
    switch (plan) {
    case connectivity::AiPlan::Plus:
        return "PLUS";
    case connectivity::AiPlan::Business:
        return "BUSINESS";
    case connectivity::AiPlan::Enterprise:
        return "ENTERPRISE";
    default:
        return "ACCOUNT";
    }
}
const char* metricName(connectivity::AiMetricKind kind) {
    switch (kind) {
    case connectivity::AiMetricKind::FiveHour:
        return "5 HOUR";
    case connectivity::AiMetricKind::Week:
        return "WEEK";
    case connectivity::AiMetricKind::Credits:
        return "CREDITS";
    case connectivity::AiMetricKind::Money:
        return "PERSONAL SPEND";
    }
    return "USAGE";
}
RgbColor quotaColor(std::uint8_t percent) {
    if (percent >= 51)
        return palette::leaf;
    if (percent >= 20)
        return palette::blue;
    return palette::vermilion;
}
void label(IDisplayAdapter& display, int x, int y, const char* text, RgbColor color = palette::ink,
           std::uint8_t scale = 1) {
    display.drawText({x, y}, text, {color, palette::bone, scale});
}
void providerTitle(IDisplayAdapter& display, int x, int y,
                   const connectivity::AiUsageProvider& provider, std::uint8_t scale = 1) {
    const char* name = provider.provider == connectivity::AiProvider::Codex ? "CODEX" : "CURSOR";
    const auto width = static_cast<int>(std::strlen(name)) * 6 * scale;
    label(display, x, y, name, palette::ink, scale);
    display.fillRectangle({x + width + 8 * scale, y + 3 * scale}, 2 * scale, 2 * scale,
                          palette::ink);
    label(display, x + width + 18 * scale, y, planName(provider.plan), palette::ink, scale);
}
void number(char* out, std::size_t size, std::uint32_t value) {
    std::snprintf(out, size, "%lu", static_cast<unsigned long>(value));
}
void groupedNumber(char (&out)[14], std::uint32_t value) {
    char digits[16]{};
    number(digits, sizeof(digits), value);
    const auto length = std::strlen(digits);
    std::size_t position = 0;
    for (std::size_t i = 0; i < length; ++i) {
        if (i != 0 && (length - i) % 3 == 0)
            out[position++] = ',';
        out[position++] = digits[i];
    }
    out[position] = '\0';
}
void money(char* out, std::size_t size, std::uint32_t cents) {
    char grouped[14]{};
    groupedNumber(grouped, cents / 100);
    std::snprintf(out, size, "$%s.%02lu", grouped, static_cast<unsigned long>(cents % 100));
}
void valueText(char* out, std::size_t size, const connectivity::AiUsageMetric& metric) {
    if (metric.unit == connectivity::AiMetricUnit::Cents) {
        char used[20]{};
        char limit[20]{};
        money(used, sizeof(used), metric.used);
        money(limit, sizeof(limit), metric.limit);
        std::snprintf(out, size, "%s / %s", used, limit);
    } else if (metric.unit == connectivity::AiMetricUnit::Credits) {
        char used[14]{};
        char limit[14]{};
        groupedNumber(used, metric.used);
        groupedNumber(limit, metric.limit);
        std::snprintf(out, size, "%s / %s CR", used, limit);
    } else
        number(out, size, metric.used);
}
void resetText(char* out, std::size_t size, std::uint32_t resetAt, std::uint32_t seconds) {
    if (resetAt == 0) {
        std::snprintf(out, size, "RESET --");
        return;
    }
    if (seconds >= 86400)
        std::snprintf(out, size, "RESET %luD %luH", static_cast<unsigned long>(seconds / 86400),
                      static_cast<unsigned long>((seconds % 86400) / 3600));
    else
        std::snprintf(out, size, "RESET %luH %luM", static_cast<unsigned long>(seconds / 3600),
                      static_cast<unsigned long>((seconds % 3600) / 60));
}
} // namespace

void AiUsageApp::onActivate() {
    rendered_ = false;
    selected_ = false;
    selectionRemaining_ = {};
    countdownElapsed_ = {};
    countdownSeconds_ = 0;
}

void AiUsageApp::onDeactivate() {
    indicator_.clearFocus();
    rendered_ = false;
    selected_ = false;
}

void AiUsageApp::drawMetric(const connectivity::AiUsageMetric& metric, int top, bool compact) {
    char text[72]{};
    const auto percent = metric.remainingPercent;
    const auto color = quotaColor(percent);
    if (compact) {
        valueText(text, sizeof(text), metric);
        label(display_, 8, top + 24, text);
        std::snprintf(text, sizeof(text), "%u%% LEFT%s", percent, percent < 5 ? " !" : "");
        label(display_, core::rightAlignedTextX(text, 232), top + 24, text, color);
        display_.fillRectangle({8, top + 40}, 224, 7, core::palette::pale);
        display_.fillRectangle({8, top + 40}, 224 * percent / 100, 7, color);
        resetText(text, sizeof(text), metric.resetAt,
                  metric.resetRemainingSeconds > countdownSeconds_
                      ? metric.resetRemainingSeconds - countdownSeconds_
                      : 0);
        label(display_, 8, top + 53, text, core::palette::ordinal);
    } else {
        label(display_, 8, top, metricName(metric.kind));
        std::snprintf(text, sizeof(text), "%u%% LEFT%s", percent, percent < 5 ? " !" : "");
        label(display_, core::rightAlignedTextX(text, 232), top, text, color);
        display_.fillRectangle({8, top + 15}, 224, 7, core::palette::pale);
        display_.fillRectangle({8, top + 15}, 224 * percent / 100, 7, color);
        resetText(text, sizeof(text), metric.resetAt,
                  metric.resetRemainingSeconds > countdownSeconds_
                      ? metric.resetRemainingSeconds - countdownSeconds_
                      : 0);
        label(display_, 8, top + 27, text, core::palette::ordinal);
    }
}

void AiUsageApp::draw() {
    const auto& snapshot = usage_.snapshot();
    display_.beginFrame();
    display_.clear(core::palette::bone);
    if (!usage_.available() || snapshot.state == connectivity::AiUsageState::Discovering) {
        label(display_, 71, 58, "CHECKING AI", core::palette::ink, 2);
    } else if (snapshot.providerCount == 0) {
        label(display_, 44, 48, "NO AI ACCOUNTS", core::palette::ink, 2);
        label(display_, 40, 82, "CODEX / CURSOR NOT AVAILABLE", core::palette::ordinal);
    } else if (snapshot.providerCount == 1 && snapshot.providers[0].metricCount == 2) {
        const auto& provider = snapshot.providers[0];
        providerTitle(display_, 8, 6, provider);
        if (provider.freshness == connectivity::AiFreshness::Stale)
            label(display_, 199, 6, "STALE", core::palette::vermilion);
        drawMetric(provider.metrics[0], 28, false);
        drawMetric(provider.metrics[1], 83, false);
        if (selected_)
            display_.fillRectangle({3, selection_ == 0 ? 30 : 85}, 2, 9, core::palette::blue);
    } else if (snapshot.providerCount == 1) {
        const auto& provider = snapshot.providers[0];
        providerTitle(display_, 8, 12, provider, 2);
        if (provider.freshness == connectivity::AiFreshness::Stale)
            label(display_, 196, 12, "STALE", core::palette::vermilion);
        if (provider.metricCount > 0)
            drawMetric(provider.metrics[0], 47, false);
    } else {
        for (std::uint8_t i = 0; i < 2; ++i) {
            const auto& provider = snapshot.providers[i];
            const auto top = i ? 68 : 0;
            providerTitle(display_, 8, top + 6, provider);
            if (provider.freshness == connectivity::AiFreshness::Stale)
                label(display_, 198, top + 6, "STALE", core::palette::vermilion);
            if (provider.metricCount > 0)
                drawMetric(provider.metrics[0], top, true);
            if (selected_ && selection_ == i)
                display_.fillRectangle({3, top + 6}, 2, 9, core::palette::blue);
        }
        display_.fillRectangle({8, 67}, 224, 1, core::palette::pale);
    }
    display_.endFrame();
}

void AiUsageApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    if (usage_.session() != renderedSession_) {
        renderedSession_ = usage_.session();
        selected_ = false;
        indicator_.clearFocus();
        rendered_ = false;
    }
    const auto& snapshot = usage_.snapshot();
    const auto visible = snapshot.providerCount == 2   ? 2
                         : snapshot.providerCount == 1 ? snapshot.providers[0].metricCount
                                                       : 0;
    for (const auto& event : input) {
        if (event.type != core::InputEventType::NamedKey || visible == 0)
            continue;
        if (event.namedKey != core::NamedKey::Up && event.namedKey != core::NamedKey::Down)
            continue;
        if (!selected_)
            selection_ =
                event.namedKey == core::NamedKey::Up ? static_cast<std::uint8_t>(visible - 1) : 0;
        else
            selection_ = static_cast<std::uint8_t>(
                (selection_ + (event.namedKey == core::NamedKey::Up ? visible - 1 : 1)) % visible);
        selected_ = true;
        selectionRemaining_ = std::chrono::seconds(3);
        indicator_.focus(selection_);
        rendered_ = false;
    }
    if (selected_) {
        selectionRemaining_ = elapsed >= selectionRemaining_ ? std::chrono::milliseconds(0)
                                                             : selectionRemaining_ - elapsed;
        if (selectionRemaining_.count() == 0) {
            selected_ = false;
            rendered_ = false;
        }
    }
    if (usage_.revision() != renderedRevision_) {
        renderedRevision_ = usage_.revision();
        countdownElapsed_ = {};
        countdownSeconds_ = 0;
        rendered_ = false;
    }
    countdownElapsed_ += elapsed;
    if (countdownElapsed_ >= std::chrono::seconds(60)) {
        countdownElapsed_ %= std::chrono::seconds(60);
        countdownSeconds_ += 60;
        rendered_ = false;
    }
    if (!rendered_) {
        draw();
        rendered_ = true;
    }
}

} // namespace cardputer_hub::apps
