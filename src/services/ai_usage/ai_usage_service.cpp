#include "services/ai_usage/ai_usage_service.h"

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
    if (left.state != right.state || left.providerCount != right.providerCount)
        return false;
    for (std::uint8_t i = 0; i < left.providerCount; ++i) {
        const auto& a = left.providers[i];
        const auto& b = right.providers[i];
        if (a.provider != b.provider || a.plan != b.plan || a.freshness != b.freshness ||
            a.metricCount != b.metricCount)
            return false;
        for (std::uint8_t j = 0; j < a.metricCount; ++j)
            if (!sameMetric(a.metrics[j], b.metrics[j]))
                return false;
    }
    return true;
}
} // namespace

void AiUsageService::clear() {
    snapshot_ = {};
    inFlight_ = false;
    requestId_ = 0;
    sincePoll_ = {};
    sinceSample_ = {};
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
        if (!samePresentation(snapshot_, next)) {
            snapshot_ = next;
            ++revision_;
        }
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
    if (sincePoll_ >= pollInterval) {
        sincePoll_ = {};
        request();
    }
}

} // namespace cardputer_hub::services
