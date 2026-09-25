#include "services/mac_status/mac_status_service.h"

namespace cardputer_hub::services {
using connectivity::CompanionOperation;
using connectivity::CompanionStatus;

void MacStatusService::startMonitoring() {
    while (companion_.takeCompletedRequest(CompanionOperation::SystemMetrics)) {
    }
    monitoring_ = true;
    inFlight_ = false;
    pollElapsed_ = {};
    sinceSample_ = {};
    snapshot_ = {};
    request();
}

void MacStatusService::stopMonitoring() {
    monitoring_ = false;
    inFlight_ = false;
    inFlightId_ = 0;
    pollElapsed_ = {};
    snapshot_.freshness = MacStatusFreshness::Empty;
}

void MacStatusService::request() {
    if (!monitoring_ || inFlight_ ||
        companion_.hasPendingRequest(CompanionOperation::SystemMetrics))
        return;
    if (companion_.requestSystemMetrics() != CompanionSubmitResult::Submitted)
        return;
    inFlight_ = true;
    inFlightId_ = companion_.lastSubmittedRequestId();
}

void MacStatusService::accept(const connectivity::CompanionSystemMetrics& metrics) {
    const auto flags = metrics.validity;
    snapshot_.cpuAvailable = flags & 1U;
    snapshot_.cpuPercent = metrics.cpuPercent;
    snapshot_.memoryAvailable = flags & 2U;
    snapshot_.memoryUsedMiB = metrics.memoryUsedMiB;
    snapshot_.memoryTotalMiB = metrics.memoryTotalMiB;
    snapshot_.memoryPressure = flags & 4U ? static_cast<MacMemoryPressure>(metrics.memoryPressure)
                                          : MacMemoryPressure::Unknown;
    snapshot_.diskAvailable = flags & 8U;
    snapshot_.diskUsedPercent = metrics.diskUsedPercent;
    snapshot_.batteryAvailable = flags & 16U;
    snapshot_.batteryPercent = metrics.batteryPercent;
    snapshot_.networkAvailable = flags & 32U;
    snapshot_.downloadKiBps = metrics.downloadKiBps;
    snapshot_.uploadKiBps = metrics.uploadKiBps;
    snapshot_.thermalState =
        flags & 64U ? static_cast<MacThermalState>(metrics.thermalState) : MacThermalState::Unknown;
    snapshot_.freshness = MacStatusFreshness::Fresh;
    ++snapshot_.generation;
    sinceSample_ = {};
}

void MacStatusService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero())
        elapsed = {};
    if (monitoring_ && snapshot_.freshness == MacStatusFreshness::Fresh) {
        sinceSample_ += elapsed;
        if (sinceSample_ > freshnessTimeout) {
            snapshot_.freshness = MacStatusFreshness::Stale;
            ++snapshot_.generation;
        }
    }
    while (const auto completion =
               companion_.takeCompletedRequest(CompanionOperation::SystemMetrics)) {
        if (!monitoring_ || !inFlight_ || completion->requestId != inFlightId_)
            continue;
        inFlight_ = false;
        if (completion->status != CompanionStatus::Ok)
            continue;
        connectivity::CompanionSystemMetrics metrics{};
        if (connectivity::readSystemMetrics(completion->message, metrics))
            accept(metrics);
    }
    if (!monitoring_)
        return;
    pollElapsed_ += elapsed;
    if (pollElapsed_ >= pollInterval) {
        pollElapsed_ %= pollInterval;
        request();
    }
}

} // namespace cardputer_hub::services
