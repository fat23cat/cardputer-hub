#include "apps/shell/application_shell.h"
#include "apps/shell/home_graphics.h"
#include "core/display/palette.h"
#include <algorithm>

namespace cardputer_hub::apps {
using namespace core;

ApplicationShell::ApplicationShell(services::HostService& hosts, ActionBus& actions,
                                   IDisplayAdapter& display, HostSettings& settings,
                                   services::AudioService& audio)
    : hosts_(hosts), actions_(actions), display_(display), settings_(settings), audio_(audio) {
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
            settingsFrame_.reset();
        } else if (atBluetooth()) {
            display_.beginTransition(SlideDirection::Backward);
            (void)navigation_.back();
            settingsFrame_.reset();
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
            homeConnectionFrame_.reset();
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
        const bool left =
            (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Left) ||
            (event.type == InputEventType::PrintableCharacter && event.character == ',' &&
             !event.modifiers.shift);
        const bool right =
            (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Right) ||
            (event.type == InputEventType::PrintableCharacter && event.character == '/' &&
             !event.modifiers.shift);
        const bool volumeStep = atSettings() && plain && settingsSelection_ == 1 && (left || right);
        if (volumeStep) {
            const auto previousVolume = audio_.volume();
            const bool changesVolume = right ? previousVolume < 100 : previousVolume > 0;
            const auto result =
                actions_.dispatch({"audio.volume.step",
                                   "settings",
                                   {{"delta", static_cast<std::int32_t>(right ? 10 : -10)}}});
            if (result == DispatchResult::Handled && changesVolume) {
                (void)audio_.play(right ? services::AudioCue::StepRight
                                        : services::AudioCue::StepLeft);
            } else {
                (void)audio_.play(services::AudioCue::KeyPress);
            }
        } else {
            (void)audio_.play(services::AudioCue::KeyPress);
        }
        const bool settingsChord = plain && !event.modifiers.shift && !event.modifiers.fn &&
                                   event.type == InputEventType::NamedKey &&
                                   event.namedKey == NamedKey::Tab &&
                                   (atHome() || atSettings() || atBluetooth());
        if (settingsChord) {
            (void)actions_.dispatch({"ui.settings", "shell", {}});
        } else if (atBluetooth()) {
            settings_.update({event});
        } else if (atSettings() && plain) {
            const bool up =
                (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Up) ||
                (event.type == InputEventType::PrintableCharacter && event.character == ';' &&
                 !event.modifiers.shift);
            const bool down =
                (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Down) ||
                (event.type == InputEventType::PrintableCharacter && event.character == '.' &&
                 !event.modifiers.shift);
            if (up || down) {
                const auto nextSelection = static_cast<std::uint8_t>(down ? 1 : 0);
                if (nextSelection != settingsSelection_) {
                    settingsSelection_ = nextSelection;
                }
            } else if (event.type == InputEventType::NamedKey &&
                       event.namedKey == NamedKey::Enter && settingsSelection_ == 0)
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
    const SettingsFrame next{settingsSelection_, audio_.volume()};
    if (settingsFrame_ && settingsFrame_->selection == next.selection &&
        settingsFrame_->volume == next.volume)
        return;
    const bool full = !settingsFrame_;
    const TextStyle normal{palette::ink, palette::bone, 1};
    const TextStyle selected{palette::bone, palette::ink, 1};
    if (full) {
        display_.clear(palette::bone);
        display_.drawText({6, 6}, "SETTINGS", normal);
        display_.fillRectangle({6, 20}, 228, 1, palette::ink);
    }
    const auto drawRow = [&](std::uint8_t index, std::int32_t y, const char* ordinal,
                             const char* label, const std::string& value) {
        const bool focusChanged =
            settingsFrame_ && (settingsFrame_->selection == index) != (next.selection == index);
        const bool valueChanged =
            index == 1 && settingsFrame_ && settingsFrame_->volume != next.volume;
        if (!full && !focusChanged && !valueChanged)
            return;
        const bool focused = settingsSelection_ == index;
        const auto style = focused ? selected : normal;
        display_.fillRectangle({6, y}, 228, 16, focused ? palette::ink : palette::bone);
        display_.drawText({10, y + 3}, ordinal, style);
        display_.drawText({30, y + 3}, label, style);
        if (!value.empty())
            display_.drawText({230 - static_cast<std::int32_t>(value.size()) * 6, y + 3},
                              value.c_str(), style);
    };
    drawRow(0, 24, "01", "Bluetooth", {});
    drawRow(1, 42, "02", "Sound volume", std::to_string(audio_.volume()) + "%");
    settingsFrame_ = next;
}

void ApplicationShell::renderHome(std::chrono::milliseconds elapsed,
                                  std::optional<std::uint8_t> batteryPercent) {
    const auto hostStatus = hosts_.status();
    static const std::string noHostSelected = "No host selected";
    const auto& name =
        hostStatus.activeHostName.empty() ? noHostSelected : hostStatus.activeHostName;
    const char* state = "ERROR";
    RgbColor accent = palette::vermilion;
    switch (hostStatus.connection) {
    case services::HostConnectionStatus::Off:
        state = "OFF";
        accent = palette::ordinal;
        break;
    case services::HostConnectionStatus::Connecting:
        state = "CONNECTING";
        accent = palette::blue;
        break;
    case services::HostConnectionStatus::Securing:
        state = "SECURING";
        accent = palette::blue;
        break;
    case services::HostConnectionStatus::Ready:
        state = "READY";
        accent = palette::leaf;
        break;
    case services::HostConnectionStatus::Pairing:
        state = "PAIRING";
        accent = palette::blue;
        break;
    case services::HostConnectionStatus::Error:
        state = "ERROR";
        accent = palette::vermilion;
        break;
    }
    const bool entering = !homeConnectionFrame_;
    const bool connectionChanged = entering ||
                                   homeConnectionFrame_->activeHost != hostStatus.activeHostId ||
                                   homeConnectionFrame_->hostName != name ||
                                   homeConnectionFrame_->status != hostStatus.connection;
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
    if (connectionChanged) {
        display_.fillRectangle({8, 32}, 224, 65, palette::bone);
        display_.drawText({8, 38}, "SELECTED HOST", quiet);
        drawHomeHostName(display_, name);
        drawBluetoothIcon(display_, accent);
        display_.drawText({29, 84}, state, normal);
        homeConnectionFrame_ =
            HomeConnectionFrame{hostStatus.activeHostId, name, hostStatus.connection};
    }
    const auto percent = batteryPercent && *batteryPercent <= 100 ? batteryPercent : std::nullopt;
    if (entering || percent != homeBatteryPercent_) {
        const auto battery = percent ? std::to_string(*percent) + "%" : std::string("--%");
        display_.fillRectangle({184, 5}, 48, 14, palette::bone);
        display_.drawText({232 - static_cast<int>(battery.size()) * 6, 8}, battery.c_str(), normal);
        homeBatteryPercent_ = percent;
    }
    const auto oldStep = homePhaseMilliseconds_ / 500;
    const auto advance = static_cast<unsigned>(std::max<std::int64_t>(0, elapsed.count()) % 28000);
    homePhaseMilliseconds_ = (homePhaseMilliseconds_ + advance) % 28000;
    if (entering || oldStep != homePhaseMilliseconds_ / 500)
        drawHomeWave(display_, (homePhaseMilliseconds_ / 500) * 500);
}
} // namespace cardputer_hub::apps
