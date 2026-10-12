#include "apps/shell/status_sheet.h"
#include "apps/hosts/host_settings.h"
#include "apps/network/wifi_settings.h"
#include "core/display/palette.h"
#include "core/display/screen_header.h"
#include "core/display/text_layout.h"
#include <cstring>

namespace cardputer_hub::apps {
using namespace core;

namespace {
// A value has at most 16 glyphs (112 px), so its dot never meets a label.
constexpr std::size_t valueColumns = 16;
constexpr std::int32_t headerRegionHeight = 23;
constexpr std::int32_t rowPitch = 20;
// Text sits 5 px below the top of its row region.
constexpr std::int32_t rowTextInset = 5;
constexpr std::int32_t sheetBottom = headerRegionHeight + rowPitch * statusSheetRowCount;
// The dot's right edge sits 4 px before the value's first glyph.
constexpr std::int32_t dotOffset = 9;

bool sameRow(const StatusSheetRow& left, const StatusSheetRow& right) {
    return std::strcmp(left.ordinal, right.ordinal) == 0 &&
           std::strcmp(left.label, right.label) == 0 && left.indicator == right.indicator &&
           left.value == right.value && left.valueColor.red == right.valueColor.red &&
           left.valueColor.green == right.valueColor.green &&
           left.valueColor.blue == right.valueColor.blue;
}

StatusSheetRow row(const char* ordinal, const char* label,
                   std::optional<HomeStatusIndicator> indicator, std::string value) {
    const bool failure = indicator == HomeStatusIndicator::FilledVermilion;
    return {ordinal, label, indicator, fitSystemTextColumns(value, valueColumns),
            failure ? palette::vermilion : palette::ink};
}

std::string bluetoothValue(const services::HostStatusSnapshot& host) {
    if (host.connection == services::HostConnectionStatus::Ready)
        return host.activeHostName.empty() ? hostConnectionWord(host.connection)
                                           : host.activeHostName;
    if (host.connection == services::HostConnectionStatus::Off && host.radioHeld)
        return bluetoothSwitchWord(host);
    return hostConnectionWord(host.connection);
}

StatusSheetRow companionRow(const StatusSheetInputs& inputs) {
    const auto state = inputs.companion.value_or(services::CompanionServiceState::Unavailable);
    switch (state) {
    case services::CompanionServiceState::Ready:
        return row("03", "COMPANION", HomeStatusIndicator::FilledLeaf, "READY");
    case services::CompanionServiceState::Attaching:
    case services::CompanionServiceState::Handshaking:
        return row("03", "COMPANION", HomeStatusIndicator::FilledBlue, "CONNECTING");
    case services::CompanionServiceState::ProtocolError:
        return row("03", "COMPANION", HomeStatusIndicator::FilledVermilion, "ERROR");
    case services::CompanionServiceState::Incompatible:
        return row("03", "COMPANION", HomeStatusIndicator::FilledVermilion,
                   homeCompanionMismatchText(inputs.companionAdvice));
    case services::CompanionServiceState::Unavailable:
        break;
    }
    const bool bluetoothReady = inputs.host.connection == services::HostConnectionStatus::Ready;
    return row("03", "COMPANION", HomeStatusIndicator::HollowQuiet,
               bluetoothReady ? "OFFLINE" : "WAITING BT");
}

std::string webValue(services::WebAccessState state, std::size_t clients) {
    if (state == services::WebAccessState::Off)
        return "OFF";
    if (state == services::WebAccessState::Error)
        return "ERROR";
    if (clients == 0)
        return "ON";
    return std::to_string(clients) + (clients == 1 ? " CLIENT" : " CLIENTS");
}

void drawRow(IDisplayAdapter& display, std::size_t index, const StatusSheetRow& model) {
    const auto top = headerRegionHeight + rowPitch * static_cast<std::int32_t>(index);
    const auto y = top + rowTextInset;
    display.beginOverlayRegion({{0, top}, 240, rowPitch});
    display.fillRectangle({0, top}, 240, rowPitch, palette::bone);
    display.drawText({6, y}, model.ordinal, {palette::ordinal, palette::bone, systemTextScale});
    display.drawText({26, y}, model.label, {palette::ink, palette::bone, systemTextScale});
    const auto valueX = rightAlignedTextX(model.value.c_str());
    if (model.indicator)
        drawHomeIndicatorDot(display, {valueX - dotOffset, y + 2}, *model.indicator);
    display.drawText({valueX, y}, model.value.c_str(),
                     {model.valueColor, palette::bone, systemTextScale});
    display.endOverlayRegion();
}
} // namespace

StatusSheetModel buildStatusSheetModel(const StatusSheetInputs& inputs) {
    // An estimate above 100% reads as unknown, as on Home.
    const unsigned percent = inputs.batteryPercent.value_or(255);
    const bool known = percent <= 100;
    auto battery = row("05", "BATTERY", std::nullopt,
                       known ? std::to_string(percent) + "%" : std::string("--%"));
    if (known && percent <= 10)
        battery.valueColor = palette::vermilion;
    const auto web = homeWebIndicator(inputs.web).value_or(HomeStatusIndicator::HollowQuiet);
    return {row("01", "WI-FI", homeWifiIndicator(inputs.wifi), wifiStatusWord(inputs.wifi)),
            row("02", "BLUETOOTH", homeBluetoothIndicator(inputs.host.connection),
                bluetoothValue(inputs.host)),
            companionRow(inputs), row("04", "WEB", web, webValue(inputs.web, inputs.webClients)),
            battery};
}

void StatusSheetView::render(IDisplayAdapter& display, const StatusSheetModel& model) {
    const bool full = !drawn_;
    if (full) {
        display.beginOverlayRegion({{0, 0}, 240, headerRegionHeight});
        display.fillRectangle({0, 0}, 240, headerRegionHeight, palette::bone);
        drawScreenHeader(display, "STATUS", "G0 CLOSE");
        display.endOverlayRegion();
    }
    for (std::size_t index = 0; index < model.size(); ++index)
        if (full || !sameRow((*drawn_)[index], model[index]))
            drawRow(display, index, model[index]);
    if (full) {
        display.beginOverlayRegion({{0, sheetBottom}, 240, 135 - sheetBottom});
        display.fillRectangle({0, sheetBottom}, 240, 135 - sheetBottom, palette::bone);
        display.endOverlayRegion();
    }
    drawn_ = model;
}

} // namespace cardputer_hub::apps
