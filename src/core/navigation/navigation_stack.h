#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cardputer_hub::core {

using RouteId = std::string;

enum class NavigationResult : std::uint8_t {
    Navigated,
    InvalidRoute,
};

enum class BackResult : std::uint8_t {
    Popped,
    AtRoot,
    Empty,
};

class NavigationStack {
  public:
    [[nodiscard]] NavigationResult resetTo(RouteId routeId);
    [[nodiscard]] NavigationResult push(RouteId routeId);
    [[nodiscard]] BackResult back();
    [[nodiscard]] const RouteId* current() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;

  private:
    std::vector<RouteId> history_;
};

} // namespace cardputer_hub::core
