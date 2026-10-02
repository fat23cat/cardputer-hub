#include "services/storage/removable_storage_service.h"

namespace cardputer_hub::services {

RemovableStorageService::RemovableStorageService(core::FileStorage& files,
                                                 core::CapabilityRegistry& capabilities,
                                                 core::Logger* logger,
                                                 RemovableStorageConfig config)
    : files_(files), capabilities_(capabilities), logger_(logger), config_(config) {}

bool RemovableStorageService::attempt() {
    sinceAttempt_ = std::chrono::milliseconds(0);
    const auto mounted = files_.refresh();
    if (logger_ != nullptr) {
        logger_->log(
            {mounted == core::FileStorageState::Ready ? core::LogLevel::Info
                                                      : core::LogLevel::Warning,
             "storage",
             mounted == core::FileStorageState::Ready ? "microSD ready" : "microSD unavailable"});
    }
    syncCapability();
    return mounted == core::FileStorageState::Ready;
}

bool RemovableStorageService::ensureReady() {
    if (ready()) {
        syncCapability();
        return true;
    }
    if (sinceAttempt_ && *sinceAttempt_ < config_.retryInterval) {
        syncCapability();
        return false;
    }
    return attempt();
}

bool RemovableStorageService::retryNow() { return attempt(); }

void RemovableStorageService::update(std::chrono::milliseconds elapsed) {
    if (sinceAttempt_)
        *sinceAttempt_ += elapsed;
    syncCapability();
}

// An operation that finds the card gone moves the adapter out of Ready; the
// capability follows on the next update.
void RemovableStorageService::syncCapability() {
    const bool available = ready();
    if (available == published_)
        return;
    if (available)
        (void)capabilities_.registerCapability(removableFileStorageCapabilityId);
    else
        (void)capabilities_.removeCapability(removableFileStorageCapabilityId);
    published_ = available;
}

} // namespace cardputer_hub::services
