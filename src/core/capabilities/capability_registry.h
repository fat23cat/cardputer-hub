#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cardputer_hub::core {

using CapabilityId = std::string;

enum class CapabilityRegistrationResult : std::uint8_t {
    Registered,
    InvalidId,
    DuplicateId,
};

enum class CapabilityRemovalResult : std::uint8_t {
    Removed,
    InvalidId,
    NotFound,
};

class CapabilityRegistry {
  public:
    [[nodiscard]] CapabilityRegistrationResult registerCapability(CapabilityId capabilityId);
    [[nodiscard]] CapabilityRemovalResult removeCapability(const CapabilityId& capabilityId);
    [[nodiscard]] bool isAvailable(const CapabilityId& capabilityId) const;
    [[nodiscard]] const std::vector<CapabilityId>& availableCapabilities() const noexcept;

  private:
    std::vector<CapabilityId> capabilities_;
};

} // namespace cardputer_hub::core
