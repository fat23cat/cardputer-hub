#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cardputer_hub::core {

struct AppDescriptor {
    std::string id;
    std::string displayName;
    std::string iconId;
    std::string entryRoute;
    std::vector<std::string> requiredCapabilities;
};

enum class AppRegistrationResult : std::uint8_t {
    Registered,
    InvalidDescriptor,
    DuplicateId,
};

class AppRegistry {
  public:
    [[nodiscard]] AppRegistrationResult registerApp(AppDescriptor descriptor);
    [[nodiscard]] const AppDescriptor* find(const std::string& id) const;
    [[nodiscard]] const std::vector<AppDescriptor>& apps() const noexcept;

  private:
    std::vector<AppDescriptor> apps_;
};

} // namespace cardputer_hub::core
