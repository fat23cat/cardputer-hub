#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cardputer_hub::apps {

inline constexpr std::uint8_t macControlSlotCount = 6;
inline constexpr char macControlAppId[] = "mac-control";
inline constexpr char telegramBundleId[] = "com.tdesktop.Telegram";

struct MacControlBinding {
    std::uint8_t slot = 0;
    std::string_view label;
    std::string_view iconId;
    std::string_view bundleId;
};

struct MacControlPage {
    std::array<std::optional<MacControlBinding>, macControlSlotCount> slots{};
};

[[nodiscard]] std::uint8_t macControlSlotForKey(char digit) noexcept;
[[nodiscard]] const MacControlBinding* macControlBindingAt(const MacControlPage& page,
                                                           std::uint8_t slot);
[[nodiscard]] bool macControlCanMovePrevious(std::size_t pageIndex) noexcept;
[[nodiscard]] bool macControlCanMoveNext(std::size_t pageIndex, std::size_t pageCount) noexcept;
[[nodiscard]] std::vector<MacControlPage> productionMacControlPages();
[[nodiscard]] MacControlPage macControlPageWithBinding(MacControlBinding binding);

} // namespace cardputer_hub::apps
