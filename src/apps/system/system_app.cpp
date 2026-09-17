#include "apps/system/system_app.h"

#include "core/display/palette.h"
#include "core/display/text_layout.h"
#include "core/lifecycle/build_info.h"

namespace cardputer_hub::apps {
using namespace core;

namespace {
std::string uppercase(std::string text) {
    for (auto& character : text)
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    return text;
}

std::string fitValue(std::string text, std::size_t maximum) {
    text = uppercase(std::move(text));
    if (text.size() <= maximum)
        return text;
    return text.substr(0, maximum - 3) + "...";
}

const char* hostStatusText(services::HostConnectionStatus status) {
    switch (status) {
    case services::HostConnectionStatus::Off:
        return "OFF";
    case services::HostConnectionStatus::Connecting:
        return "CONNECTING";
    case services::HostConnectionStatus::Securing:
        return "SECURING";
    case services::HostConnectionStatus::Ready:
        return "READY";
    case services::HostConnectionStatus::Pairing:
        return "PAIRING";
    case services::HostConnectionStatus::Error:
        return "ERROR";
    }
    return "ERROR";
}
} // namespace

const char* systemWifiStatusText(const services::WifiStatusSnapshot& status) {
    if (!status.enabled)
        return "OFF";
    if (!status.configured)
        return "ON";
    switch (status.connection) {
    case services::WifiConnectionStatus::Connecting:
        return "CONNECTING";
    case services::WifiConnectionStatus::Connected:
        return "CONNECTED";
    case services::WifiConnectionStatus::Error:
        return "ERROR";
    case services::WifiConnectionStatus::Off:
        return "ON";
    }
    return "ON";
}

SystemApp::SystemApp(services::BatteryService& battery, services::HostService& hosts,
                     services::NetworkService& network, IDisplayAdapter& display)
    : battery_(battery), hosts_(hosts), network_(network), display_(display) {}

void SystemApp::onActivate() { frame_.reset(); }

void SystemApp::onDeactivate() { frame_.reset(); }

void SystemApp::update(const InputEvents&, std::chrono::milliseconds) {
    const auto next = capture();
    if (frame_ && frame_->battery == next.battery && frame_->bluetooth == next.bluetooth &&
        frame_->host == next.host && frame_->hostStatus == next.hostStatus &&
        frame_->wifi == next.wifi && frame_->network == next.network &&
        frame_->version == next.version)
        return;
    render(next);
    frame_ = next;
}

SystemApp::Frame SystemApp::capture() const {
    Frame frame;
    const auto percent = battery_.percent();
    frame.battery = percent && *percent <= 100 ? std::to_string(*percent) + "%" : "--%";
    const auto host = hosts_.status();
    frame.bluetooth = host.connectionEnabled ? "ON" : "OFF";
    frame.host = host.activeHostName.empty() ? "NONE" : fitValue(host.activeHostName, 16);
    frame.hostStatus = hostStatusText(host.connection);
    const auto wifi = network_.status();
    frame.wifi = systemWifiStatusText(wifi);
    frame.network = wifi.configured && !wifi.ssid.empty() ? fitValue(wifi.ssid, 16) : "NONE";
    frame.version = firmwareBuildInfo().version;
    return frame;
}

void SystemApp::render(const Frame& next) {
    const TextStyle normal{palette::ink, palette::bone, 1};
    const bool full = !frame_;
    if (full) {
        display_.clear(palette::bone);
        display_.drawText({6, 6}, "SYSTEM", normal);
        display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    }
    const auto drawRow = [&](std::int32_t y, const char* ordinal, const char* label,
                             const std::string& value, const std::string* previous) {
        if (!full && previous != nullptr && *previous == value)
            return;
        display_.fillRectangle({6, y}, 228, 16, palette::bone);
        display_.drawText({10, y + 3}, ordinal, normal);
        display_.drawText({30, y + 3}, label, normal);
        display_.drawText({rightAlignedTextX(value.c_str()), y + 3}, value.c_str(), normal);
    };
    drawRow(22, "01", "BATTERY", next.battery, frame_ ? &frame_->battery : nullptr);
    drawRow(38, "02", "BLUETOOTH", next.bluetooth, frame_ ? &frame_->bluetooth : nullptr);
    drawRow(54, "03", "HOST", next.host, frame_ ? &frame_->host : nullptr);
    drawRow(70, "04", "HOST STATUS", next.hostStatus, frame_ ? &frame_->hostStatus : nullptr);
    drawRow(86, "05", "WI-FI", next.wifi, frame_ ? &frame_->wifi : nullptr);
    drawRow(102, "06", "NETWORK", next.network, frame_ ? &frame_->network : nullptr);
    drawRow(118, "07", "VERSION", next.version, frame_ ? &frame_->version : nullptr);
}

} // namespace cardputer_hub::apps
