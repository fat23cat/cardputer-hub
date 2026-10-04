#pragma once

#include "core/capabilities/capability_registry.h"
#include "core/logging/logger.h"
#include "core/storage/files/file_storage.h"

#include <chrono>
#include <optional>

namespace cardputer_hub::services {

inline constexpr char removableFileStorageCapabilityId[] = "REMOVABLE_FILE_STORAGE";

struct RemovableStorageConfig {
    // The least time between automatic mount attempts while the card is absent.
    std::chrono::milliseconds retryInterval{3000};
};

// Owns when the microSD card is mounted and publishes its live state. Nothing
// mounts at boot: a consumer asks for the card when it needs it, and a missing
// card is retried at most once per interval, so it never stalls the main loop
// repeatedly. Card state is independent of every other device.
class RemovableStorageService {
  public:
    RemovableStorageService(core::FileStorage& files, core::CapabilityRegistry& capabilities,
                            core::Logger* logger = nullptr, RemovableStorageConfig config = {});

    // True when the card is mounted. Otherwise mounts it, unless the last
    // attempt was less than one retry interval ago.
    bool ensureReady();
    // Mounts again now, for an explicit user retry.
    bool retryNow();
    void update(std::chrono::milliseconds elapsed);

    [[nodiscard]] core::FileStorageState state() const { return files_.state(); }
    [[nodiscard]] bool ready() const { return files_.state() == core::FileStorageState::Ready; }
    [[nodiscard]] core::FileStorage& files() noexcept { return files_; }

  private:
    bool attempt();
    void syncCapability();

    core::FileStorage& files_;
    core::CapabilityRegistry& capabilities_;
    core::Logger* logger_;
    RemovableStorageConfig config_;
    std::optional<std::chrono::milliseconds> sinceAttempt_;
    bool published_ = false;
};

} // namespace cardputer_hub::services
