#include "core/capabilities/capability_registry.h"

#include <algorithm>
#include <utility>

namespace cardputer_hub::core {
namespace {

bool isValidCapabilityId(const CapabilityId& capabilityId) { return !capabilityId.empty(); }

template <typename Capabilities>
auto findCapability(Capabilities& capabilities, const CapabilityId& capabilityId) {
    return std::find(capabilities.begin(), capabilities.end(), capabilityId);
}

} // namespace

CapabilityRegistrationResult CapabilityRegistry::registerCapability(CapabilityId capabilityId) {
    if (!isValidCapabilityId(capabilityId)) {
        return CapabilityRegistrationResult::InvalidId;
    }
    if (findCapability(capabilities_, capabilityId) != capabilities_.end()) {
        return CapabilityRegistrationResult::DuplicateId;
    }

    capabilities_.push_back(std::move(capabilityId));
    return CapabilityRegistrationResult::Registered;
}

CapabilityRemovalResult CapabilityRegistry::removeCapability(const CapabilityId& capabilityId) {
    if (!isValidCapabilityId(capabilityId)) {
        return CapabilityRemovalResult::InvalidId;
    }

    const auto capability = findCapability(capabilities_, capabilityId);
    if (capability == capabilities_.end()) {
        return CapabilityRemovalResult::NotFound;
    }

    capabilities_.erase(capability);
    return CapabilityRemovalResult::Removed;
}

bool CapabilityRegistry::isAvailable(const CapabilityId& capabilityId) const {
    if (!isValidCapabilityId(capabilityId)) {
        return false;
    }
    return findCapability(capabilities_, capabilityId) != capabilities_.end();
}

const std::vector<CapabilityId>& CapabilityRegistry::availableCapabilities() const noexcept {
    return capabilities_;
}

} // namespace cardputer_hub::core
