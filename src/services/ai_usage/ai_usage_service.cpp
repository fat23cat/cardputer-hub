#include "services/ai_usage/ai_usage_service.h"

#include <algorithm>
#include <cstring>

namespace cardputer_hub::services {
namespace {
bool sameMetric(const connectivity::AiUsageMetric& left,
                const connectivity::AiUsageMetric& right) noexcept {
    return left.kind == right.kind && left.unit == right.unit && left.used == right.used &&
           left.limit == right.limit && left.remaining == right.remaining &&
           left.remainingPercent == right.remainingPercent && left.resetAt == right.resetAt;
}

bool samePresentation(const connectivity::CompanionAiUsage& left,
                      const connectivity::CompanionAiUsage& right) noexcept {
    if (left.schemaVersion != right.schemaVersion || left.state != right.state ||
        left.providerCount != right.providerCount)
        return false;
    for (std::uint8_t i = 0; i < left.providerCount; ++i) {
        const auto& a = left.providers[i];
        const auto& b = right.providers[i];
        if (a.provider != b.provider || a.plan != b.plan || a.freshness != b.freshness ||
            a.metricCount != b.metricCount)
            return false;
        const auto& ar = a.resetCredits;
        const auto& br = b.resetCredits;
        if (ar.known != br.known || ar.availableCount != br.availableCount ||
            ar.creditCount != br.creditCount)
            return false;
        for (std::uint8_t j = 0; j < ar.creditCount; ++j)
            if (std::strcmp(ar.credits[j].title.data(), br.credits[j].title.data()) != 0 ||
                ar.credits[j].expiresAt != br.credits[j].expiresAt)
                return false;
        for (std::uint8_t j = 0; j < a.metricCount; ++j)
            if (!sameMetric(a.metrics[j], b.metrics[j]))
                return false;
    }
    return true;
}

bool resetLabelChanges(std::uint32_t oldSeconds, std::uint32_t newSeconds) noexcept {
    if (oldSeconds >= 86400 || newSeconds >= 86400)
        return (oldSeconds >= 86400) != (newSeconds >= 86400) ||
               oldSeconds / 3600 != newSeconds / 3600;
    return oldSeconds / 60 != newSeconds / 60;
}
} // namespace

void AiUsageService::clear() {
    snapshot_ = {};
    inFlight_ = false;
    requestId_ = 0;
    sincePoll_ = {};
    sinceSample_ = {};
    countdownElapsed_ = {};
    ++revision_;
}

void AiUsageService::request() {
    if (!available_ || inFlight_ ||
        companion_.hasPendingRequest(connectivity::CompanionOperation::AiUsage))
        return;
    if (companion_.requestAiUsage() == CompanionSubmitResult::Submitted) {
        inFlight_ = true;
        requestId_ = companion_.lastSubmittedRequestId();
        sincePoll_ = {};
    }
}

void AiUsageService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero())
        elapsed = {};
    const auto currentSession =
        companion_.hasLiveCompanion() && companion_.selectedProtocolVersion() >= 3
            ? companion_.session()
            : 0;
    const auto currentAvailable = companion_.supportsAiUsage();
    if (currentSession != session_ || currentAvailable != available_) {
        session_ = currentSession;
        available_ = currentAvailable;
        clear();
        request();
    }
    countdownElapsed_ += elapsed;
    const auto passedSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(countdownElapsed_).count();
    if (passedSeconds > 0) {
        countdownElapsed_ -= std::chrono::seconds(passedSeconds);
        const auto step = static_cast<std::uint64_t>(passedSeconds);
        for (std::uint8_t i = 0; i < snapshot_.providerCount; ++i) {
            auto& provider = snapshot_.providers[i];
            for (std::uint8_t j = 0; j < provider.metricCount; ++j) {
                auto& remaining = provider.metrics[j].resetRemainingSeconds;
                remaining = step >= remaining ? 0 : remaining - static_cast<std::uint32_t>(step);
            }
            for (std::uint8_t j = 0; j < provider.resetCredits.creditCount; ++j) {
                auto& remaining = provider.resetCredits.credits[j].expiresRemainingSeconds;
                remaining = step >= remaining ? 0 : remaining - static_cast<std::uint32_t>(step);
            }
        }
    }
    while (const auto completion =
               companion_.takeCompletedRequest(connectivity::CompanionOperation::AiUsage)) {
        if (!available_ || !inFlight_ || completion->requestId != requestId_)
            continue;
        inFlight_ = false;
        if (completion->status != connectivity::CompanionStatus::Ok)
            continue;
        connectivity::CompanionAiUsage next{};
        if (!connectivity::readAiUsage(completion->message, next))
            continue;
        sinceSample_ = {};
        bool timerCorrection = false;
        bool newReset = false;
        for (std::uint8_t i = 0; i < next.providerCount; ++i) {
            auto& provider = next.providers[i];
            for (std::uint8_t j = 0; j < provider.metricCount; ++j) {
                auto& metric = provider.metrics[j];
                if (metric.resetAt == 0) {
                    metric.resetRemainingSeconds = 0;
                    continue;
                }
                if (i < snapshot_.providerCount &&
                    provider.provider == snapshot_.providers[i].provider &&
                    j < snapshot_.providers[i].metricCount &&
                    metric.kind == snapshot_.providers[i].metrics[j].kind &&
                    metric.resetAt == snapshot_.providers[i].metrics[j].resetAt) {
                    const auto oldSeconds = snapshot_.providers[i].metrics[j].resetRemainingSeconds;
                    metric.resetRemainingSeconds =
                        std::min(oldSeconds, metric.resetRemainingSeconds);
                    timerCorrection |= resetLabelChanges(oldSeconds, metric.resetRemainingSeconds);
                } else {
                    newReset = true;
                }
            }
            for (std::uint8_t j = 0; j < provider.resetCredits.creditCount; ++j) {
                auto& credit = provider.resetCredits.credits[j];
                if (credit.expiresAt == 0) {
                    credit.expiresRemainingSeconds = 0;
                    continue;
                }
                if (i < snapshot_.providerCount &&
                    provider.provider == snapshot_.providers[i].provider &&
                    j < snapshot_.providers[i].resetCredits.creditCount &&
                    credit.expiresAt == snapshot_.providers[i].resetCredits.credits[j].expiresAt) {
                    credit.expiresRemainingSeconds = std::min(
                        credit.expiresRemainingSeconds,
                        snapshot_.providers[i].resetCredits.credits[j].expiresRemainingSeconds);
                }
            }
        }
        const bool presentationChanged = !samePresentation(snapshot_, next);
        snapshot_ = next;
        if (newReset)
            countdownElapsed_ = {};
        if (presentationChanged || timerCorrection)
            ++revision_;
    }
    if (!available_)
        return;
    sincePoll_ += elapsed;
    sinceSample_ += elapsed;
    if (sinceSample_ >= freshnessTimeout) {
        bool changed = false;
        for (std::uint8_t i = 0; i < snapshot_.providerCount; ++i) {
            auto& provider = snapshot_.providers[i];
            if (provider.freshness != connectivity::AiFreshness::Stale) {
                provider.freshness = connectivity::AiFreshness::Stale;
                changed = true;
            }
        }
        if (changed)
            ++revision_;
    }
    const auto interval = snapshot_.state == connectivity::AiUsageState::Discovering
                              ? discoveryPollInterval
                              : pollInterval;
    if (sincePoll_ >= interval) {
        sincePoll_ = {};
        request();
    }
}

} // namespace cardputer_hub::services
