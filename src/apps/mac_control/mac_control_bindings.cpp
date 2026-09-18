#include "apps/mac_control/mac_control_bindings.h"

namespace cardputer_hub::apps {

std::uint8_t macControlSlotForKey(char digit) noexcept {
    if (digit < '1' || digit > '6')
        return 0;
    return static_cast<std::uint8_t>(digit - '0');
}

const MacControlBinding* macControlBindingAt(const MacControlPage& page, std::uint8_t slot) {
    if (slot < 1 || slot > macControlSlotCount)
        return nullptr;
    const auto& bound = page.slots[slot - 1];
    return bound ? &*bound : nullptr;
}

bool macControlCanMovePrevious(std::size_t pageIndex) noexcept { return pageIndex > 0; }

bool macControlCanMoveNext(std::size_t pageIndex, std::size_t pageCount) noexcept {
    return pageCount > 0 && pageIndex + 1 < pageCount;
}

MacControlPage macControlPageWithBinding(MacControlBinding binding) {
    MacControlPage page;
    if (binding.slot >= 1 && binding.slot <= macControlSlotCount)
        page.slots[binding.slot - 1] = binding;
    return page;
}

std::vector<MacControlPage> productionMacControlPages() {
    return {macControlPageWithBinding({1, "TELEGRAM", "", telegramBundleId})};
}

} // namespace cardputer_hub::apps
