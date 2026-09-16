#pragma once

#include <cstring>

namespace cardputer_hub::apps::assets {

inline constexpr int appIconSize = 14;

inline constexpr const char* const systemAppIcon[] = {
    "      ##      ", " ##  ####  ## ", " ############ ", "###  ####  ###", "##   #  #   ##",
    "##   #  #   ##", "##############", "##############", "##   #  #   ##", "##   #  #   ##",
    "###  ####  ###", " ############ ", " ##  ####  ## ", "      ##      ",
};

inline constexpr const char* const fallbackAppIcon[] = {
    " ############ ", " #          # ", " # ######## # ", " # #      # # ", " # #      # # ",
    " # #      # # ", " # #      # # ", " # #      # # ", " # ######## # ", " #          # ",
    " #          # ", " #          # ", " #          # ", " ############ ",
};

inline const char* const* appIconRows(const char* iconId) {
    if (iconId != nullptr && std::strcmp(iconId, "system") == 0)
        return systemAppIcon;
    return fallbackAppIcon;
}

} // namespace cardputer_hub::apps::assets
