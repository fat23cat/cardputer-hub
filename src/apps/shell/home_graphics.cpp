#include "apps/shell/home_graphics.h"
#include "core/display/palette.h"
#include <string>

namespace cardputer_hub::apps {
namespace {
void drawDot(core::IDisplayAdapter& display, core::PixelPosition position,
             HomeStatusIndicator indicator) {
    const bool hollow = indicator == HomeStatusIndicator::HollowQuiet;
    const core::RgbColor color =
        indicator == HomeStatusIndicator::FilledPale        ? core::palette::pale
        : indicator == HomeStatusIndicator::FilledBlue      ? core::palette::blue
        : indicator == HomeStatusIndicator::FilledLeaf      ? core::palette::leaf
        : indicator == HomeStatusIndicator::FilledVermilion ? core::palette::vermilion
                                                            : core::palette::ordinal;
    static const char* const filled[] = {" ### ", "#####", "#####", "#####", " ### "};
    static const char* const outline[] = {" ### ", "#   #", "#   #", "#   #", " ### "};
    const auto rows = hollow ? outline : filled;
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            if (rows[y][x] == '#')
                display.fillRectangle({position.x + x, position.y + y}, 1, 1, color);
}
} // namespace

HomeStatusIndicator homeWifiIndicator(const services::WifiStatusSnapshot& status) {
    if (!status.configured)
        return HomeStatusIndicator::HollowQuiet;
    if (!status.enabled || status.connection == services::WifiConnectionStatus::Off)
        return HomeStatusIndicator::FilledPale;
    switch (status.connection) {
    case services::WifiConnectionStatus::Connecting:
        return HomeStatusIndicator::FilledBlue;
    case services::WifiConnectionStatus::Connected:
        return HomeStatusIndicator::FilledLeaf;
    case services::WifiConnectionStatus::Error:
        return HomeStatusIndicator::FilledVermilion;
    case services::WifiConnectionStatus::Off:
        return HomeStatusIndicator::FilledPale;
    }
    return HomeStatusIndicator::FilledPale;
}

HomeStatusIndicator homeBluetoothIndicator(services::HostConnectionStatus status) {
    switch (status) {
    case services::HostConnectionStatus::Off:
        return HomeStatusIndicator::HollowQuiet;
    case services::HostConnectionStatus::Connecting:
    case services::HostConnectionStatus::Securing:
    case services::HostConnectionStatus::Pairing:
        return HomeStatusIndicator::FilledBlue;
    case services::HostConnectionStatus::Ready:
        return HomeStatusIndicator::FilledLeaf;
    case services::HostConnectionStatus::Error:
        return HomeStatusIndicator::FilledVermilion;
    }
    return HomeStatusIndicator::HollowQuiet;
}

void drawHomeStatusBar(core::IDisplayAdapter& display) {
    display.fillRectangle({0, 21}, 240, 1, core::palette::ink);
    const core::TextStyle label{core::palette::ink, core::palette::bone, 1};
    display.drawText({18, 6}, "WiFi", label);
    display.drawText({72, 6}, "BT", label);
}

void drawHomeWifi(core::IDisplayAdapter& display, HomeStatusIndicator indicator) {
    display.fillRectangle(homeWifiDotPosition, 5, 5, core::palette::bone);
    drawDot(display, homeWifiDotPosition, indicator);
}

void drawHomeBluetooth(core::IDisplayAdapter& display, HomeStatusIndicator indicator) {
    display.fillRectangle(homeBluetoothDotPosition, 5, 5, core::palette::bone);
    drawDot(display, homeBluetoothDotPosition, indicator);
}

std::string homeConnectedDeviceName(const services::HostStatusSnapshot& status) {
    if (status.connection != services::HostConnectionStatus::Ready || status.activeHostName.empty())
        return {};
    // The normal system font is six pixels wide. Keep the stored profile untouched.
    constexpr std::size_t maxCharacters = (240 - 20 - 8) / 6;
    if (status.activeHostName.size() <= maxCharacters)
        return status.activeHostName;
    return status.activeHostName.substr(0, maxCharacters - 3) + "...";
}

void drawHomeConnectedDevice(core::IDisplayAdapter& display, const std::string& name) {
    display.fillRectangle(homeDeviceRowOrigin, 240, homeDeviceRowHeight, core::palette::bone);
    if (name.empty())
        return;
    drawDot(display, {8, 101}, HomeStatusIndicator::FilledLeaf);
    display.drawText({20, 100}, name.c_str(), {core::palette::ink, core::palette::bone, 1});
}

void drawHomeActions(core::IDisplayAdapter& display, std::int32_t plateX) {
    display.fillRectangle(homeActionBarOrigin, 240, homeActionBarHeight, core::palette::bone);
    display.fillRectangle({0, 112}, 240, 1, core::palette::ink);
    display.fillRectangle({119, 113}, 1, 22, core::palette::ink);
    display.fillRectangle({plateX, 116}, 112, 16, core::palette::ink);
    const auto drawAction = [&](int x, int width, const char* label, bool onPlate) {
        const core::RgbColor background = onPlate ? core::palette::ink : core::palette::bone;
        const core::RgbColor foreground = onPlate ? core::palette::bone : core::palette::ink;
        const int textWidth = static_cast<int>(std::string(label).size()) * 6;
        display.drawText({x + (width - textWidth) / 2, 120}, label, {foreground, background, 1});
    };
    const bool settingsOnPlate = plateX >= 64;
    drawAction(0, 119, "APPS", !settingsOnPlate);
    drawAction(120, 120, "SETTINGS", settingsOnPlate);
}

void drawHomeBattery(core::IDisplayAdapter& display, std::optional<std::uint8_t> batteryPercent) {
    const auto label = batteryPercent && *batteryPercent <= 100
                           ? std::to_string(*batteryPercent) + "%"
                           : std::string("--%");
    display.fillRectangle({202, 4}, 30, 14, core::palette::bone);
    display.drawText({232 - static_cast<int>(label.size()) * 6, 6}, label.c_str(),
                     {core::palette::ink, core::palette::bone, 1});
}
} // namespace cardputer_hub::apps
