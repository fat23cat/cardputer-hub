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

void drawHomeCompanion(core::IDisplayAdapter& display, bool visible) {
    display.fillRectangle(homeCompanionPosition, homeCompanionIndicatorSize,
                          homeCompanionIndicatorSize, core::palette::bone);
    if (!visible)
        return;
    for (int y = 0; y < homeCompanionIndicatorSize; ++y) {
        const int radius = 4 - (y > 4 ? y - 4 : 4 - y);
        display.fillRectangle({homeCompanionPosition.x + 4 - radius, homeCompanionPosition.y + y},
                              radius * 2 + 1, 1, core::palette::leaf);
    }
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
