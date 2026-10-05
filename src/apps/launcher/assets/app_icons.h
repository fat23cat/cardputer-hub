#pragma once

#include <cstring>

namespace cardputer_hub::apps::assets {

inline constexpr int appIconSize = 14;

inline constexpr const char* const systemAppIcon[] = {
    "      ##      ", " ##  ####  ## ", " ############ ", "###  ####  ###", "##   #  #   ##",
    "##   #  #   ##", "##############", "##############", "##   #  #   ##", "##   #  #   ##",
    "###  ####  ###", " ############ ", " ##  ####  ## ", "      ##      ",
};

inline constexpr const char* const macControlAppIcon[] = {
    "##############", "#  ##  ##  ## ", "#  ##  ##  ## ", "##############", "#  ##  ##  ## ",
    "#  ##  ##  ## ", "##############", "##############", "#  ##  ##  ## ", "#  ##  ##  ## ",
    "##############", "#  ##  ##  ## ", "#  ##  ##  ## ", "##############",
};

inline constexpr const char* const macStatusAppIcon[] = {
    "              ", "              ", "         ##   ", "         ##   ", "   ##    ##   ",
    "   ##    ##   ", "   ##    ##   ", "   ##    ##   ", "   ## ## ##   ", "   ## ## ##   ",
    "   ## ## ##   ", "   ## ## ##   ", "##############", "              ",
};

inline constexpr const char* const aiUsageAppIcon[] = {
    "              ", " ############ ", " #          # ", " #       ## # ", " #       ## # ",
    " #     #### # ", " #     #### # ", " #   ###### # ", " #   ###### # ", " # ######## # ",
    " # ######## # ", " # ######## # ", " ############ ", "              ",
};

// Three status rows: a dot and a label bar each.
inline constexpr const char* const aiStatusAppIcon[] = {
    "              ", " ##           ", "####  ####### ", "####  ####### ", " ##           ",
    "              ", " ##           ", "####  ####### ", "####  ####### ", " ##           ",
    "              ", " ##           ", "####  ####### ", "####  ####### ",
};

inline constexpr const char* const pomodoroAppIcon[] = {
    "      ##      ", "     #  #     ", "   ########   ", "  ##########  ", " ############ ",
    "##############", "##############", "##############", "##############", "##############",
    " ############ ", "  ##########  ", "   ########   ", "    ######    ",
};

inline constexpr const char* const ledGalleryAppIcon[] = {
    "#  #  #  #  # ", "  #  #  #  #  ", "#  #  #  #  # ", "  #  #  #  #  ", "#  #  #  #  # ",
    "  #  #  #  #  ", "#  #  #  #  # ", "  #  #  #  #  ", "#  #  #  #  # ", "  #  #  #  #  ",
    "#  #  #  #  # ", "  #  #  #  #  ", "#  #  #  #  # ", "  #  #  #  #  ",
};

inline constexpr const char* const nfcAppIcon[] = {
    "              ", "         #    ", "          #   ", "      #   #   ", "       #   #  ",
    "   #   #   #  ", "##  #   #  #  ", "##  #   #  #  ", "   #   #   #  ", "       #   #  ",
    "      #   #   ", "          #   ", "         #    ", "              ",
};

inline constexpr const char* const fallbackAppIcon[] = {
    " ############ ", " #          # ", " # ######## # ", " # #      # # ", " # #      # # ",
    " # #      # # ", " # #      # # ", " # #      # # ", " # ######## # ", " #          # ",
    " #          # ", " #          # ", " #          # ", " ############ ",
};

inline const char* const* appIconRows(const char* iconId) {
    if (iconId != nullptr && std::strcmp(iconId, "system") == 0)
        return systemAppIcon;
    if (iconId != nullptr && std::strcmp(iconId, "mac-control") == 0)
        return macControlAppIcon;
    if (iconId != nullptr && std::strcmp(iconId, "mac-status") == 0)
        return macStatusAppIcon;
    if (iconId != nullptr && std::strcmp(iconId, "ai-usage") == 0)
        return aiUsageAppIcon;
    if (iconId != nullptr && std::strcmp(iconId, "ai-status") == 0)
        return aiStatusAppIcon;
    if (iconId != nullptr && std::strcmp(iconId, "pomodoro") == 0)
        return pomodoroAppIcon;
    if (iconId != nullptr && std::strcmp(iconId, "led-gallery") == 0)
        return ledGalleryAppIcon;
    if (iconId != nullptr && std::strcmp(iconId, "nfc") == 0)
        return nfcAppIcon;
    return fallbackAppIcon;
}

} // namespace cardputer_hub::apps::assets
