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
    case connectivity::AiPlan::Pro:
        return "PRO";
    case connectivity::AiPlan::Max:
        return "MAX";
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
const char* shortMetricName(connectivity::AiMetricKind kind) {
    return kind == connectivity::AiMetricKind::FiveHour ? "5H"
           : kind == connectivity::AiMetricKind::Week   ? "WK"
                                                        : "--";
}
bool rolling(const connectivity::AiUsageProvider& provider) {
    for (std::uint8_t i = 0; i < provider.metricCount; ++i)
        if (provider.metrics[i].kind == connectivity::AiMetricKind::FiveHour ||
            provider.metrics[i].kind == connectivity::AiMetricKind::Week)
            return true;
    return false;
}
bool plus(const connectivity::AiUsageProvider& provider) {
    return provider.provider == connectivity::AiProvider::Codex &&
           provider.plan == connectivity::AiPlan::Plus;
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
    display.drawText(
        {x, y}, text,
        {color, palette::bone, scale == 1 ? systemTextScale : static_cast<float>(scale)});
}
// Returns the right edge of the title.
int providerTitle(IDisplayAdapter& display, int x, int y,
                  const connectivity::AiUsageProvider& provider, std::uint8_t scale = 1) {
    const char* name = provider.provider == connectivity::AiProvider::Codex    ? "CODEX"
                       : provider.provider == connectivity::AiProvider::Claude ? "CLAUDE"
                                                                               : "CURSOR";
    const auto textScale = scale == 1 ? systemTextScale : static_cast<float>(scale);
    const auto width = textWidth(name, textScale);
    label(display, x, y, name, palette::ink, scale);
    display.fillRectangle({x + width + 8 * scale, y + 3 * scale}, 2 * scale, 2 * scale,
                          palette::ink);
    const char* plan = planName(provider.plan);
    label(display, x + width + 18 * scale, y, plan, palette::ink, scale);
    return x + width + 18 * scale + textWidth(plan, textScale);
}
void resetBadgeAt(IDisplayAdapter& display, int x, int y,
                  const connectivity::AiUsageProvider& provider) {
    if (!plus(provider) || !provider.resetCredits.known)
        return;
    char count[8]{};
    std::snprintf(count, sizeof(count), "%u",
                  static_cast<unsigned>(provider.resetCredits.availableCount));
    label(display, x, y, "R", palette::blue);
    for (int i = 0; i < 5; ++i) {
        display.fillRectangle({x + 7 + i, y + 2 + i}, 1, 1, palette::blue);
        display.fillRectangle({x + 11 - i, y + 2 + i}, 1, 1, palette::blue);
    }
    label(display, x + 14, y, count, palette::blue);
}
void resetBadge(IDisplayAdapter& display, int y, const connectivity::AiUsageProvider& provider) {
    char count[8]{};
    std::snprintf(count, sizeof(count), "%u",
                  static_cast<unsigned>(provider.resetCredits.availableCount));
    const int right = provider.freshness == connectivity::AiFreshness::Stale ? 188 : 232;
    resetBadgeAt(display, right - systemTextWidth(count) - 14, y, provider);
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
void durationText(char* out, std::size_t size, std::uint32_t resetAt, std::uint32_t seconds) {
    if (resetAt == 0)
        std::snprintf(out, size, "--");
    else if (seconds >= 86400)
        std::snprintf(out, size, "%luD %luH", static_cast<unsigned long>(seconds / 86400),
                      static_cast<unsigned long>((seconds % 86400) / 3600));
    else
        std::snprintf(out, size, "%luH %luM", static_cast<unsigned long>(seconds / 3600),
                      static_cast<unsigned long>((seconds % 3600) / 60));
}
void resetText(char* out, std::size_t size, std::uint32_t resetAt, std::uint32_t seconds) {
    char duration[24]{};
    durationText(duration, sizeof(duration), resetAt, seconds);
    std::snprintf(out, size, "RESET %s", duration);
}
void expiryText(char* out, std::size_t size, std::uint32_t expiresAt,
                std::uint32_t remainingSeconds) {
    if (expiresAt == 0)
        std::snprintf(out, size, "EXP --");
    else if (remainingSeconds >= 86400)
        std::snprintf(out, size, "EXP IN %luD",
                      static_cast<unsigned long>(remainingSeconds / 86400));
    else if (remainingSeconds >= 3600)
        std::snprintf(out, size, "EXP IN %luH",
                      static_cast<unsigned long>(remainingSeconds / 3600));
    else if (remainingSeconds >= 60)
        std::snprintf(out, size, "EXP IN %luM", static_cast<unsigned long>(remainingSeconds / 60));
    else if (remainingSeconds > 0)
        std::snprintf(out, size, "EXP IN 1M");
    else
        std::snprintf(out, size, "EXP NOW");
}
} // namespace

void AiUsageApp::onActivate() {
    indicator_.activate();
    rendered_ = false;
    selected_ = false;
    selectionRemaining_ = {};
    countdownElapsed_ = {};
    view_ = View::Main;
    resetScroll_ = 0;
}

void AiUsageApp::onDeactivate() {
    indicator_.deactivate();
    rendered_ = false;
    selected_ = false;
    view_ = View::Main;
    resetScroll_ = 0;
}

const connectivity::AiUsageProvider* AiUsageApp::detailProvider() const {
    const auto& snapshot = usage_.snapshot();
    for (std::uint8_t i = 0; i < snapshot.providerCount; ++i) {
        const auto& provider = snapshot.providers[i];
        if (provider.provider == detail_ && rolling(provider))
            return &provider;
    }
    return nullptr;
}

bool AiUsageApp::handleBack() {
    if (!showingDetails())
        return false;
    view_ = View::Main;
    resetScroll_ = 0;
    rendered_ = false;
    return true;
}

void AiUsageApp::drawExpanded(const connectivity::AiUsageProvider& provider) {
    char text[72]{};
    if (view_ == View::Limits) {
        providerTitle(display_, 8, 6, provider);
        display_.fillRectangle({120, 26}, 1, 81, core::palette::pale);
        for (std::uint8_t i = 0; i < 2; ++i) {
            const int left = i == 0 ? 8 : 128;
            const auto kind =
                i == 0 ? connectivity::AiMetricKind::FiveHour : connectivity::AiMetricKind::Week;
            label(display_, left, 26, metricName(kind), core::palette::blue);
            const connectivity::AiUsageMetric* metric = nullptr;
            for (std::uint8_t j = 0; j < provider.metricCount; ++j)
                if (provider.metrics[j].kind == kind)
                    metric = &provider.metrics[j];
            if (metric == nullptr) {
                label(display_, left, 40, "--", core::palette::ink, 2);
                label(display_, left, 61, "UNAVAILABLE", core::palette::ordinal);
                label(display_, left, 84, "LEFT --", core::palette::ordinal);
                label(display_, left, 98, "RESET --", core::palette::ordinal);
                continue;
            }
            std::snprintf(text, sizeof(text), "%u%%",
                          static_cast<unsigned>(100 - metric->remainingPercent));
            label(display_, left, 40, text, core::palette::ink, 2);
            label(display_, left, 61, "USED", core::palette::ordinal);
            display_.fillRectangle({left, 74}, 104, 5, core::palette::pale);
            display_.fillRectangle({left, 74}, 104 * metric->remainingPercent / 100, 5,
                                   quotaColor(metric->remainingPercent));
            label(display_, left, 84, "LEFT");
            std::snprintf(text, sizeof(text), "%u%%", metric->remainingPercent);
            label(display_, core::rightAlignedTextX(text, left + 104), 84, text,
                  quotaColor(metric->remainingPercent));
            resetText(text, sizeof(text), metric->resetAt, metric->resetRemainingSeconds);
            label(display_, left, 98, text, core::palette::ordinal);
        }
    } else {
        label(display_, 8, 6, "RESET CREDITS");
        label(display_, 8, 23, "AVAILABLE");
        const auto& resets = provider.resetCredits;
        if (resets.known)
            number(text, sizeof(text), resets.availableCount);
        else
            std::snprintf(text, sizeof(text), "--");
        label(display_, core::rightAlignedTextX(text, 232), 23, text, core::palette::blue);
        if (!resets.known) {
            label(display_, 8, 62, "DETAILS UNAVAILABLE", core::palette::ordinal);
        } else if (resets.availableCount == 0) {
            label(display_, 8, 62, "NO RESETS AVAILABLE", core::palette::ordinal);
        } else if (resets.creditCount == 0) {
            label(display_, 8, 62, "DETAILS UNAVAILABLE", core::palette::ordinal);
        } else {
            for (std::uint8_t row = 0; row < 2 && resetScroll_ + row < resets.creditCount; ++row) {
                const auto index = static_cast<std::uint8_t>(resetScroll_ + row);
                const auto& credit = resets.credits[index];
                std::snprintf(text, sizeof(text), "#%u  %s", static_cast<unsigned>(index + 1),
                              credit.title.data());
                label(display_, 8, 44 + row * 32, text);
                expiryText(text, sizeof(text), credit.expiresAt, credit.expiresRemainingSeconds);
                label(display_, 8, 57 + row * 32, text, core::palette::ordinal);
            }
        }
    }
    if (provider.freshness == connectivity::AiFreshness::Stale)
        label(display_, 199, 6, "STALE", core::palette::vermilion);
    if (!plus(provider))
        return;
    display_.fillRectangle({8, 112}, 224, 1, core::palette::pale);
    label(display_, 38, 120, "LIMITS",
          view_ == View::Limits ? core::palette::blue : core::palette::ordinal);
    label(display_, 116, 120, "|", core::palette::ordinal);
    label(display_, 151, 120, "RESETS",
          view_ == View::Resets ? core::palette::blue : core::palette::ordinal);
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
        resetText(text, sizeof(text), metric.resetAt, metric.resetRemainingSeconds);
        label(display_, 8, top + 53, text, core::palette::ordinal);
    } else {
        label(display_, 8, top, metricName(metric.kind));
        std::snprintf(text, sizeof(text), "%u%% LEFT%s", percent, percent < 5 ? " !" : "");
        label(display_, core::rightAlignedTextX(text, 232), top, text, color);
        display_.fillRectangle({8, top + 15}, 224, 7, core::palette::pale);
        display_.fillRectangle({8, top + 15}, 224 * percent / 100, 7, color);
        resetText(text, sizeof(text), metric.resetAt, metric.resetRemainingSeconds);
        label(display_, 8, top + 27, text, core::palette::ordinal);
    }
}

// Widest row values: "100%" and a "23H 59M" reset, each 6 px from its neighbour.
constexpr int rowResetRight = 232;
constexpr int rowPercentRight = 174;
constexpr int rowBarLeft = 30;
constexpr int rowBarWidth = 108;

void AiUsageApp::drawRows(const connectivity::AiUsageProvider& provider, int top,
                          std::uint8_t first) {
    char text[32]{};
    const auto titleRight = providerTitle(display_, 8, top + 6, provider);
    resetBadgeAt(display_, titleRight + 10, top + 6, provider);
    label(display_, core::rightAlignedTextX("LEFT", rowPercentRight), top + 6, "LEFT",
          core::palette::ordinal);
    if (provider.freshness == connectivity::AiFreshness::Stale)
        label(display_, core::rightAlignedTextX("STALE", rowResetRight), top + 6, "STALE",
              core::palette::vermilion);
    else
        label(display_, core::rightAlignedTextX("RESET", rowResetRight), top + 6, "RESET",
              core::palette::ordinal);
    for (std::uint8_t j = 0; j < provider.metricCount; ++j) {
        const auto& metric = provider.metrics[j];
        const int y = top + 26 + j * 20;
        const auto percent = metric.remainingPercent;
        const auto color = quotaColor(percent);
        if (selected_ && selection_ == first + j)
            display_.fillRectangle({3, y}, 2, 10, core::palette::blue);
        label(display_, 8, y, shortMetricName(metric.kind));
        display_.fillRectangle({rowBarLeft, y + 2}, rowBarWidth, 6, core::palette::pale);
        display_.fillRectangle({rowBarLeft, y + 2}, rowBarWidth * percent / 100, 6, color);
        std::snprintf(text, sizeof(text), "%u%%%s", percent, percent < 5 ? " !" : "");
        label(display_, core::rightAlignedTextX(text, rowPercentRight), y, text, color);
        durationText(text, sizeof(text), metric.resetAt, metric.resetRemainingSeconds);
        label(display_, core::rightAlignedTextX(text, rowResetRight), y, text,
              core::palette::ordinal);
    }
}

void AiUsageApp::draw() {
    const auto& snapshot = usage_.snapshot();
    display_.beginFrame();
    display_.clear(core::palette::bone);
    if (view_ != View::Main && detailProvider() != nullptr) {
        drawExpanded(*detailProvider());
    } else if (!usage_.available() || snapshot.state == connectivity::AiUsageState::Discovering) {
        label(display_, 54, 59, "CHECKING AI", core::palette::ink, 2);
    } else if (snapshot.providerCount == 0) {
        label(display_, 44, 48, "NO AI ACCOUNTS", core::palette::ink, 2);
        const char* checked = "CODEX / CURSOR / CLAUDE";
        label(display_, core::centeredTextX(checked, 0, 240), 82, checked, core::palette::ordinal);
    } else if (snapshot.providerCount == 1 && snapshot.providers[0].metricCount == 2) {
        const auto& provider = snapshot.providers[0];
        providerTitle(display_, 8, 6, provider);
        resetBadge(display_, 6, provider);
        if (provider.freshness == connectivity::AiFreshness::Stale)
            label(display_, 199, 6, "STALE", core::palette::vermilion);
        drawMetric(provider.metrics[0], 28, false);
        drawMetric(provider.metrics[1], 83, false);
        if (selected_)
            display_.fillRectangle({3, selection_ == 0 ? 30 : 85}, 2, 9, core::palette::blue);
    } else if (snapshot.providerCount == 1) {
        const auto& provider = snapshot.providers[0];
        providerTitle(display_, 8, 12, provider, 2);
        resetBadge(display_, 12, provider);
        if (provider.freshness == connectivity::AiFreshness::Stale)
            label(display_, 196, 33, "STALE", core::palette::vermilion);
        if (provider.metricCount > 0)
            drawMetric(provider.metrics[0], 47, false);
    } else {
        std::uint8_t first = 0;
        for (std::uint8_t i = 0; i < 2; ++i) {
            const auto& provider = snapshot.providers[i];
            const auto top = i ? 68 : 0;
            if (provider.metricCount == 2) {
                drawRows(provider, top, first);
                first = static_cast<std::uint8_t>(first + 2);
                continue;
            }
            providerTitle(display_, 8, top + 6, provider);
            resetBadge(display_, top + 6, provider);
            if (provider.freshness == connectivity::AiFreshness::Stale)
                label(display_, 198, top + 6, "STALE", core::palette::vermilion);
            if (provider.metricCount > 0)
                drawMetric(provider.metrics[0], top, true);
            if (selected_ && selection_ == first)
                display_.fillRectangle({3, top + 6}, 2, 9, core::palette::blue);
            first = static_cast<std::uint8_t>(first + provider.metricCount);
        }
        display_.fillRectangle({8, 67}, 224, 1, core::palette::pale);
    }
    display_.endFrame();
}

void AiUsageApp::update(const core::InputEvents& input, std::chrono::milliseconds elapsed) {
    if (usage_.session() != renderedSession_) {
        renderedSession_ = usage_.session();
        selected_ = false;
        view_ = View::Main;
        resetScroll_ = 0;
        indicator_.clearFocus();
        rendered_ = false;
    }
    const auto& snapshot = usage_.snapshot();
    if (view_ != View::Main && detailProvider() == nullptr) {
        view_ = View::Main;
        resetScroll_ = 0;
        rendered_ = false;
    }
    if (view_ == View::Resets && detailProvider() != nullptr) {
        const auto count = detailProvider()->resetCredits.creditCount;
        const auto maximum = count > 2 ? static_cast<std::uint8_t>(count - 2) : 0;
        if (resetScroll_ > maximum) {
            resetScroll_ = maximum;
            rendered_ = false;
        }
    }
    const auto metrics = services::aiUsageVisibleMetrics(snapshot);
    const auto visible = metrics.count;
    if (selected_ && selection_ >= visible) {
        selected_ = false;
        indicator_.clearFocus();
        rendered_ = false;
    }
    for (const auto& event : input) {
        if (event.type == core::InputEventType::PrintableCharacter &&
            (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.option ||
             event.modifiers.shift))
            continue;
        if (event.type == core::InputEventType::NamedKey &&
            event.namedKey == core::NamedKey::Enter) {
            // Hover picks the provider; without hover the first one with windows opens.
            const connectivity::AiUsageProvider* target = nullptr;
            if (view_ == View::Main && selected_ && selection_ < visible) {
                const auto& provider = snapshot.providers[metrics.items[selection_].provider];
                if (rolling(provider))
                    target = &provider;
            } else if (view_ == View::Main) {
                for (std::uint8_t i = 0; i < snapshot.providerCount && target == nullptr; ++i)
                    if (rolling(snapshot.providers[i]))
                        target = &snapshot.providers[i];
            }
            if (target != nullptr) {
                detail_ = target->provider;
                view_ = View::Limits;
                resetScroll_ = 0;
                rendered_ = false;
            } else if (view_ != View::Main) {
                view_ = View::Main;
                rendered_ = false;
            }
            continue;
        }
        // Fn+arrow arrives as a named key; the same keys without Fn are `;` and `.`.
        const bool down = (event.type == core::InputEventType::NamedKey &&
                           event.namedKey == core::NamedKey::Down) ||
                          (event.type == core::InputEventType::PrintableCharacter &&
                           !event.modifiers.fn && event.character == '.');
        const bool up = (event.type == core::InputEventType::NamedKey &&
                         event.namedKey == core::NamedKey::Up) ||
                        (event.type == core::InputEventType::PrintableCharacter &&
                         !event.modifiers.fn && event.character == ';');
        if (view_ != View::Main) {
            const bool horizontal =
                (event.type == core::InputEventType::NamedKey &&
                 (event.namedKey == core::NamedKey::Left ||
                  event.namedKey == core::NamedKey::Right)) ||
                (event.type == core::InputEventType::PrintableCharacter && !event.modifiers.fn &&
                 (event.character == ',' || event.character == '/'));
            if (horizontal && detailProvider() != nullptr && plus(*detailProvider())) {
                display_.beginTransition(core::isPageLeft(event) ? core::SlideDirection::Backward
                                                                 : core::SlideDirection::Forward);
                view_ = view_ == View::Limits ? View::Resets : View::Limits;
                resetScroll_ = 0;
                rendered_ = false;
            } else if (view_ == View::Resets && detailProvider() != nullptr) {
                const auto count = detailProvider()->resetCredits.creditCount;
                if (down && resetScroll_ + 2 < count) {
                    ++resetScroll_;
                    rendered_ = false;
                } else if (up && resetScroll_ > 0) {
                    --resetScroll_;
                    rendered_ = false;
                }
            }
            continue;
        }
        if (visible == 0 || (!up && !down))
            continue;
        if (!selected_)
            selection_ = up ? static_cast<std::uint8_t>(visible - 1) : 0;
        else
            selection_ = static_cast<std::uint8_t>((selection_ + (up ? visible - 1 : 1)) % visible);
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
        rendered_ = false;
    }
    countdownElapsed_ += elapsed;
    if (countdownElapsed_ >= std::chrono::seconds(60)) {
        countdownElapsed_ %= std::chrono::seconds(60);
        rendered_ = false;
    }
    if (!rendered_) {
        draw();
        rendered_ = true;
    }
}

} // namespace cardputer_hub::apps
