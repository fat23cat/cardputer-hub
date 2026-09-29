#include "services/mac_status/mac_status_service.h"

namespace cardputer_hub::services {
using connectivity::CompanionOperation;
using connectivity::CompanionStatus;
using connectivity::SystemDetailsGroup;

namespace {
SystemDetailsGroup wireGroup(MacDetailGroup group) {
    switch (group) {
    case MacDetailGroup::Power:
        return SystemDetailsGroup::Power;
    case MacDetailGroup::Network:
        return SystemDetailsGroup::Network;
    case MacDetailGroup::Memory:
        return SystemDetailsGroup::Memory;
    default:
        return SystemDetailsGroup::Cpu;
    }
}

MacDetailGroup serviceGroup(SystemDetailsGroup group) {
    switch (group) {
    case SystemDetailsGroup::Cpu:
        return MacDetailGroup::Cpu;
    case SystemDetailsGroup::Power:
        return MacDetailGroup::Power;
    case SystemDetailsGroup::Network:
        return MacDetailGroup::Network;
    case SystemDetailsGroup::Memory:
        return MacDetailGroup::Memory;
    }
    return MacDetailGroup::None;
}

template <typename T> std::optional<T> field(std::uint16_t validity, unsigned bit, T value) {
    return validity & (1U << bit) ? std::optional<T>(value) : std::nullopt;
}
} // namespace

void MacStatusService::startMonitoring() {
    while (companion_.takeCompletedRequest(CompanionOperation::SystemMetrics)) {
    }
    while (companion_.takeCompletedRequest(CompanionOperation::SystemDetails)) {
    }
    state_ = std::make_unique<State>();
    state_->session = companion_.session();
    resetDetails();
    request();
}

void MacStatusService::stopMonitoring() { state_.reset(); }

void MacStatusService::setDetailGroup(MacDetailGroup group) {
    if (!state_ || group == state_->detailGroup)
        return;
    state_->detailGroup = group;
    resetDetails();
    state_->detailDue = true;
    requestDetails();
}

void MacStatusService::resetDetails() {
    const auto generation = state_->details.generation + 1;
    state_->details = {};
    state_->details.generation = generation;
    state_->details.group = state_->detailGroup;
    state_->sinceDetail = {};
}

bool MacStatusService::request() {
    if (!state_ || state_->inFlight ||
        companion_.hasPendingRequest(CompanionOperation::SystemMetrics))
        return false;
    if (companion_.requestSystemMetrics() != CompanionSubmitResult::Submitted)
        return false;
    state_->inFlight = true;
    state_->inFlightId = companion_.lastSubmittedRequestId();
    return true;
}

void MacStatusService::requestDetails() {
    if (!state_ || state_->detailGroup == MacDetailGroup::None || state_->detailInFlight ||
        companion_.hasPendingRequest(CompanionOperation::SystemDetails))
        return;
    if (companion_.requestSystemDetails(wireGroup(state_->detailGroup)) !=
        CompanionSubmitResult::Submitted)
        return;
    state_->detailInFlight = true;
    state_->detailInFlightId = companion_.lastSubmittedRequestId();
    state_->detailInFlightGroup = state_->detailGroup;
    state_->detailDue = false;
    state_->sinceDetailRequest = {};
}

void MacStatusService::accept(const connectivity::CompanionSystemMetrics& metrics) {
    const auto flags = metrics.validity;
    state_->snapshot.cpuAvailable = flags & 1U;
    state_->snapshot.cpuPercent = metrics.cpuPercent;
    state_->snapshot.memoryAvailable = flags & 2U;
    state_->snapshot.memoryUsedMiB = metrics.memoryUsedMiB;
    state_->snapshot.memoryTotalMiB = metrics.memoryTotalMiB;
    state_->snapshot.memoryPressure = flags & 4U
                                          ? static_cast<MacMemoryPressure>(metrics.memoryPressure)
                                          : MacMemoryPressure::Unknown;
    state_->snapshot.diskAvailable = flags & 8U;
    state_->snapshot.diskUsedPercent = metrics.diskUsedPercent;
    state_->snapshot.batteryAvailable = flags & 16U;
    state_->snapshot.batteryPercent = metrics.batteryPercent;
    state_->snapshot.networkAvailable = flags & 32U;
    state_->snapshot.downloadKiBps = metrics.downloadKiBps;
    state_->snapshot.uploadKiBps = metrics.uploadKiBps;
    state_->snapshot.thermalState =
        flags & 64U ? static_cast<MacThermalState>(metrics.thermalState) : MacThermalState::Unknown;
    state_->snapshot.powerSource =
        flags & 128U ? static_cast<MacPowerSource>(metrics.powerSource) : MacPowerSource::Unknown;
    state_->snapshot.batteryMinutesAvailable = flags & 256U;
    state_->snapshot.batteryMinutes = metrics.batteryMinutes;
    state_->snapshot.freshness = MacStatusFreshness::Fresh;
    ++state_->snapshot.generation;
    state_->sinceSample = {};
    state_->history.append({state_->snapshot.cpuAvailable, state_->snapshot.cpuPercent,
                            state_->snapshot.networkAvailable, state_->snapshot.downloadKiBps,
                            state_->snapshot.uploadKiBps});
}

void MacStatusService::acceptDetails(const connectivity::CompanionSystemDetails& wire) {
    const auto generation = state_->details.generation + 1;
    MacStatusDetails next{};
    next.generation = generation;
    next.freshness = MacStatusFreshness::Fresh;
    next.group = serviceGroup(wire.group);
    const auto v = wire.validity;
    switch (wire.group) {
    case SystemDetailsGroup::Cpu:
        next.performancePercent = field(v, 0, wire.performancePercent);
        next.efficiencyPercent = field(v, 1, wire.efficiencyPercent);
        next.gpuPercent = field(v, 2, wire.gpuPercent);
        next.loadCenti = field(v, 3, wire.loadCenti);
        next.appsAvailable = v & 16U;
        next.appCount = wire.processCount;
        for (std::uint8_t i = 0; i < wire.processCount; ++i)
            next.apps[i] = {wire.processes[i].percent, wire.processes[i].name.data()};
        break;
    case SystemDetailsGroup::Power:
        next.systemDrawDeciwatts = field(v, 0, wire.systemDrawDeciwatts);
        next.adapterWatts = field(v, 1, wire.adapterWatts);
        next.healthPercent = field(v, 2, wire.healthPercent);
        next.cycleCount = field(v, 3, wire.cycleCount);
        next.peripheralPercent = field(v, 4, wire.peripheralPercent);
        next.peripheralName = wire.peripheralName.data();
        break;
    case SystemDetailsGroup::Network:
        next.internetRttMs = field(v, 0, wire.internetRttMs);
        next.routerRttMs = field(v, 1, wire.routerRttMs);
        next.wifiRssiDbm = field(v, 2, wire.wifiRssiDbm);
        next.wifiLinkMbps = field(v, 3, wire.wifiLinkMbps);
        next.vpnActive = field(v, 4, wire.vpnActive);
        break;
    case SystemDetailsGroup::Memory:
        next.memorySplitAvailable = v & 1U;
        next.appMiB = wire.appMiB;
        next.wiredMiB = wire.wiredMiB;
        next.compressedMiB = wire.compressedMiB;
        next.swapUsedMiB = field(v, 1, wire.swapUsedMiB);
        next.ssdAvailable = v & 4U;
        next.ssdFreeGB = wire.ssdFreeGB;
        next.ssdTotalGB = wire.ssdTotalGB;
        next.diskRatesAvailable = v & 8U;
        next.diskReadKiBps = wire.diskReadKiBps;
        next.diskWriteKiBps = wire.diskWriteKiBps;
        break;
    }
    state_->details = next;
    state_->sinceDetail = {};
}

void MacStatusService::processDetailCompletions() {
    while (const auto completion =
               companion_.takeCompletedRequest(CompanionOperation::SystemDetails)) {
        if (!state_ || !state_->detailInFlight || completion->requestId != state_->detailInFlightId)
            continue;
        state_->detailInFlight = false;
        // A completion for a page the user already left is dropped, and the
        // visible page is asked for at once instead of after the interval.
        if (state_->detailInFlightGroup != state_->detailGroup) {
            state_->detailDue = true;
            continue;
        }
        if (completion->status != CompanionStatus::Ok)
            continue;
        connectivity::CompanionSystemDetails wire{};
        if (connectivity::readSystemDetails(completion->message, wire) &&
            serviceGroup(wire.group) == state_->detailGroup)
            acceptDetails(wire);
    }
}

void MacStatusService::update(std::chrono::milliseconds elapsed) {
    if (elapsed < std::chrono::milliseconds::zero())
        elapsed = {};
    if (state_ && companion_.session() != state_->session) {
        // A new Companion session never inherits the previous Mac's history.
        state_->session = companion_.session();
        state_->history.clear();
        state_->snapshot = {};
        state_->inFlight = false;
        state_->detailInFlight = false;
        state_->detailDue = true;
        resetDetails();
        state_->pollElapsed = {};
        request();
    }
    if (state_ && state_->snapshot.freshness == MacStatusFreshness::Fresh) {
        state_->sinceSample += elapsed;
        if (state_->sinceSample > freshnessTimeout) {
            state_->snapshot.freshness = MacStatusFreshness::Stale;
            ++state_->snapshot.generation;
        }
    }
    if (state_ && state_->details.freshness == MacStatusFreshness::Fresh) {
        state_->sinceDetail += elapsed;
        if (state_->sinceDetail > detailFreshnessTimeout) {
            state_->details.freshness = MacStatusFreshness::Stale;
            ++state_->details.generation;
        }
    }
    while (const auto completion =
               companion_.takeCompletedRequest(CompanionOperation::SystemMetrics)) {
        if (!state_ || !state_->inFlight || completion->requestId != state_->inFlightId)
            continue;
        state_->inFlight = false;
        connectivity::CompanionSystemMetrics metrics{};
        if (completion->status == CompanionStatus::Ok &&
            connectivity::readSystemMetrics(completion->message, metrics))
            accept(metrics);
        else
            state_->history.appendGap();
    }
    processDetailCompletions();
    if (!state_)
        return;
    state_->pollElapsed += elapsed;
    if (state_->pollElapsed >= pollInterval) {
        state_->pollElapsed %= pollInterval;
        // A late answer keeps the line continuous; only a missed poll or a
        // failed request (above) breaks it.
        if (!state_->inFlight && !request())
            state_->history.appendGap();
    }
    state_->sinceDetailRequest += elapsed;
    if (state_->sinceDetailRequest >= detailPollInterval)
        state_->detailDue = true;
    if (state_->detailDue)
        requestDetails();
}

} // namespace cardputer_hub::services
