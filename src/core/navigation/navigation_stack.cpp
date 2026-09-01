#include "core/navigation/navigation_stack.h"

#include <utility>

namespace cardputer_hub::core {
namespace {

bool isValidRoute(const RouteId& routeId) { return !routeId.empty(); }

} // namespace

NavigationResult NavigationStack::resetTo(RouteId routeId) {
    if (!isValidRoute(routeId)) {
        return NavigationResult::InvalidRoute;
    }

    history_.clear();
    history_.push_back(std::move(routeId));
    return NavigationResult::Navigated;
}

NavigationResult NavigationStack::push(RouteId routeId) {
    if (!isValidRoute(routeId)) {
        return NavigationResult::InvalidRoute;
    }

    history_.push_back(std::move(routeId));
    return NavigationResult::Navigated;
}

BackResult NavigationStack::back() {
    if (history_.empty()) {
        return BackResult::Empty;
    }
    if (history_.size() == 1) {
        return BackResult::AtRoot;
    }

    history_.pop_back();
    return BackResult::Popped;
}

const RouteId* NavigationStack::current() const noexcept {
    if (history_.empty()) {
        return nullptr;
    }
    return &history_.back();
}

std::size_t NavigationStack::depth() const noexcept { return history_.size(); }

} // namespace cardputer_hub::core
