#include "apps/shell/application_shell.h"
#include "apps/shell/home_graphics.h"
#include "core/display/palette.h"
#include <algorithm>

namespace cardputer_hub::apps {
using namespace core;
using namespace connectivity;

ApplicationShell::ApplicationShell(services::HostService& hosts, ActionBus& actions,
                                   IDisplayAdapter& display, HostSettings& settings)
    : hosts_(hosts), actions_(actions), display_(display), settings_(settings) {
    (void)navigation_.resetTo("home");
    (void)actions_.registerHandler("ui.settings", *this);
    (void)actions_.registerHandler("ui.bluetooth", *this);
    (void)actions_.registerHandler("ui.back", *this);
}

bool ApplicationShell::atHome() const { return *navigation_.current() == "home"; }

bool ApplicationShell::atSettings() const { return *navigation_.current() == "settings"; }
bool ApplicationShell::atBluetooth() const { return *navigation_.current() == "bluetooth"; }

ActionHandlingResult ApplicationShell::handle(const Action& action) {
    if (action.id == "ui.settings") {
        if (atBluetooth() && settings_.modal())
            return ActionHandlingResult::Rejected;
        if (atHome()) {
            display_.beginTransition(SlideDirection::Forward);
            (void)navigation_.push("settings");
            settingsFrame_ = false;
        } else if (atBluetooth()) {
            display_.beginTransition(SlideDirection::Backward);
            (void)navigation_.back();
            settingsFrame_ = false;
        }
    } else if (action.id == "ui.bluetooth") {
        if (!atSettings())
            return ActionHandlingResult::Rejected;
        display_.beginTransition(SlideDirection::Forward);
        (void)navigation_.push("bluetooth");
        settings_.activate();
    } else if (action.id == "ui.back") {
        if (atBluetooth() && settings_.modal()) {
            (void)actions_.dispatch({"hosts.back", "shell", {}});
        } else if (!atHome()) {
            display_.beginTransition(SlideDirection::Backward);
            // Preserve the existing BLE list's explicit Esc Home behavior.
            (void)navigation_.resetTo("home");
            homeFrame_.clear();
        }
    } else
        return ActionHandlingResult::Rejected;
    return ActionHandlingResult::Handled;
}

void ApplicationShell::update(const InputEvents& input, std::chrono::milliseconds elapsed,
                              std::optional<std::uint8_t> batteryPercent) {
    display_.beginFrame();
    display_.advanceTransition(elapsed);
    for (const auto& event : input) {
        const bool plain = !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
        const bool settingsChord =
            plain && !event.modifiers.shift && event.type == InputEventType::NamedKey &&
            event.namedKey == NamedKey::Tab &&
            (event.modifiers.fn || atHome() || atSettings() || atBluetooth());
        const bool systemMenu =
            event.type == InputEventType::NamedKey && event.namedKey == NamedKey::SystemMenu;
        if (settingsChord || systemMenu) {
            (void)actions_.dispatch({"ui.settings", "shell", {}});
        } else if (atBluetooth()) {
            settings_.update({event});
        } else if (atSettings() && plain) {
            if (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Enter)
                (void)actions_.dispatch({"ui.bluetooth", "settings", {}});
            else if ((event.type == InputEventType::NamedKey &&
                      event.namedKey == NamedKey::Escape) ||
                     (event.type == InputEventType::PrintableCharacter && event.character == '`' &&
                      !event.modifiers.shift))
                (void)actions_.dispatch({"ui.back", "settings", {}});
        }
    }
    if (atHome())
        renderHome(display_.transitionActive() ? std::chrono::milliseconds(0) : elapsed,
                   batteryPercent);
    else if (atSettings())
        renderSettings();
    else
        settings_.update({});
    display_.endFrame();
}

void ApplicationShell::renderSettings() {
    if (settingsFrame_)
        return;
    settingsFrame_ = true;
    const TextStyle normal{palette::ink, palette::bone, 1};
    const TextStyle selected{palette::bone, palette::ink, 1};
    display_.clear(palette::bone);
    display_.drawText({6, 6}, "SETTINGS", normal);
    display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    display_.fillRectangle({6, 24}, 228, 16, palette::ink);
    display_.drawText({10, 27}, "01", selected);
    display_.drawText({30, 27}, "Bluetooth", selected);
}

void ApplicationShell::renderHome(std::chrono::milliseconds elapsed,
                                  std::optional<std::uint8_t> batteryPercent) {
    const auto& config = hosts_.settings();
    const auto host =
        std::find_if(config.hosts.begin(), config.hosts.end(),
                     [&](const auto& value) { return config.activeHost == value.id; });
    const std::string name = host == config.hosts.end() ? "No host selected" : host->name;
    std::string state = "CONNECTING";
    RgbColor accent = palette::blue;
    if (hosts_.lastResult() == services::HostResult::StorageError ||
        hosts_.lastResult() == services::HostResult::BluetoothError ||
        hosts_.lastResult() == services::HostResult::MissingBond ||
        hosts_.bluetoothState() == BluetoothState::Error) {
        state = "ERROR";
        accent = palette::vermilion;
    } else if (hosts_.bluetoothState() == BluetoothState::Disabled) {
        state = "OFF";
        accent = palette::ordinal;
    } else if (hosts_.hidState() == HidTransportState::Ready) {
        state = "READY";
        accent = palette::leaf;
    } else if (hosts_.pairing()) {
        state = "PAIRING";
    } else if (hosts_.bluetoothState() == BluetoothState::Connected) {
        state = "SECURING";
    }
    const auto frame = state + ":" + std::to_string(config.activeHost.value_or(0)) + ":" + name;
    const bool entering = homeFrame_.empty();
    const TextStyle normal{palette::ink, palette::bone, 1};
    const TextStyle quiet{palette::ordinal, palette::bone, 1};
    if (entering) {
        display_.clear(palette::bone);
        // Clock synchronization and Wi-Fi setup are not composed yet.
        display_.drawText({8, 8}, "--:--", normal);
        drawWifiOfflineIcon(display_);
        display_.drawText({104, 8}, "OFFLINE", normal);
        display_.fillRectangle({8, 24}, 224, 1, palette::ink);
    }
    if (entering || frame != homeFrame_) {
        display_.fillRectangle({8, 32}, 224, 65, palette::bone);
        display_.drawText({8, 38}, "SELECTED HOST", quiet);
        drawHomeHostName(display_, name);
        drawBluetoothIcon(display_, accent);
        display_.drawText({29, 84}, state.c_str(), normal);
        homeFrame_ = frame;
    }
    const int percent = batteryPercent && *batteryPercent <= 100 ? *batteryPercent : -1;
    const auto battery = percent < 0 ? "--%" : std::to_string(percent) + "%";
    if (entering || battery != batteryFrame_) {
        display_.fillRectangle({184, 5}, 48, 14, palette::bone);
        display_.drawText({232 - static_cast<int>(battery.size()) * 6, 8}, battery.c_str(), normal);
        batteryFrame_ = battery;
    }
    const auto oldStep = homePhaseMilliseconds_ / 500;
    const auto advance = static_cast<unsigned>(std::max<std::int64_t>(0, elapsed.count()) % 28000);
    homePhaseMilliseconds_ = (homePhaseMilliseconds_ + advance) % 28000;
    if (entering || oldStep != homePhaseMilliseconds_ / 500)
        drawHomeWave(display_, (homePhaseMilliseconds_ / 500) * 500);
}
} // namespace cardputer_hub::apps
