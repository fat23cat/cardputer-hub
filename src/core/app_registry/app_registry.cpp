#include "core/app_registry/app_registry.h"

#include <algorithm>
#include <utility>

namespace cardputer_hub::core {
namespace {

bool hasValidRequiredCapabilities(const AppDescriptor& descriptor) {
    for (auto capability = descriptor.requiredCapabilities.begin();
         capability != descriptor.requiredCapabilities.end(); ++capability) {
        if (capability->empty() || std::find(descriptor.requiredCapabilities.begin(), capability,
                                             *capability) != capability) {
            return false;
        }
    }
    return true;
}

bool isValidDescriptor(const AppDescriptor& descriptor) {
    return !descriptor.id.empty() && !descriptor.displayName.empty() &&
           !descriptor.entryRoute.empty() && hasValidRequiredCapabilities(descriptor);
}

template <typename Apps> auto findApp(Apps& apps, const std::string& id) {
    return std::find_if(apps.begin(), apps.end(),
                        [&id](const AppDescriptor& descriptor) { return descriptor.id == id; });
}

} // namespace

AppRegistrationResult AppRegistry::registerApp(AppDescriptor descriptor) {
    if (!isValidDescriptor(descriptor)) {
        return AppRegistrationResult::InvalidDescriptor;
    }
    if (findApp(apps_, descriptor.id) != apps_.end()) {
        return AppRegistrationResult::DuplicateId;
    }

    apps_.push_back(std::move(descriptor));
    return AppRegistrationResult::Registered;
}

const AppDescriptor* AppRegistry::find(const std::string& id) const {
    const auto app = findApp(apps_, id);
    return app == apps_.end() ? nullptr : &*app;
}

const std::vector<AppDescriptor>& AppRegistry::apps() const noexcept { return apps_; }

} // namespace cardputer_hub::core
