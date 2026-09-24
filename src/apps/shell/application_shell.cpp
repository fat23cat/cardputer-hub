#include "apps/shell/application_shell.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/home_graphics.h"
#include "core/display/palette.h"
#include <algorithm>
#include <variant>

namespace cardputer_hub::apps {
using namespace core;

namespace {
constexpr std::uint8_t settingsRowCount = 6;
constexpr std::uint8_t bluetoothRow = 0;
constexpr std::uint8_t wifiRow = 1;
constexpr std::uint8_t volumeRow = 2;
constexpr std::uint8_t timeoutRow = 3;
constexpr std::uint8_t screenBrightnessRow = 4;
constexpr std::uint8_t ledBrightnessRow = 5;

bool isUnmodified(const InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
}

bool isLeft(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Left) ||
           (event.type == InputEventType::PrintableCharacter && event.character == ',' &&
            !event.modifiers.shift);
}

bool isRight(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Right) ||
           (event.type == InputEventType::PrintableCharacter && event.character == '/' &&
            !event.modifiers.shift);
}
} // namespace

ApplicationShell::ApplicationShell(services::HostService& hosts, services::NetworkService& network,
                                   ActionBus& actions, IDisplayAdapter& display,
                                   HostSettings& settings, WiFiSettings& wifiSettings,
                                   services::AudioService& audio,
                                   services::DeviceSettingsService& deviceSettings,
                                   MiniAppRuntime& miniApps, CapabilityRegistry& capabilities)
    : hosts_(hosts), network_(network), actions_(actions), display_(display), settings_(settings),
      wifiSettings_(wifiSettings), audio_(audio), deviceSettings_(deviceSettings),
      miniApps_(miniApps), capabilities_(capabilities),
      launcher_(miniApps.apps(), miniApps, actions, display) {
    (void)navigation_.resetTo("home");
    (void)actions_.registerHandler("ui.settings", *this);
    (void)actions_.registerHandler("ui.bluetooth", *this);
    (void)actions_.registerHandler("ui.wifi", *this);
    (void)actions_.registerHandler("ui.launcher", *this);
    (void)actions_.registerHandler("ui.back", *this);
    (void)actions_.registerHandler("app.open", *this);
    (void)actions_.registerHandler("app.close", *this);
}

bool ApplicationShell::atHome() const { return *navigation_.current() == "home"; }

bool ApplicationShell::atSettings() const { return *navigation_.current() == "settings"; }
bool ApplicationShell::atBluetooth() const { return *navigation_.current() == "bluetooth"; }
bool ApplicationShell::atWifi() const { return *navigation_.current() == "wifi"; }
bool ApplicationShell::atLauncher() const { return *navigation_.current() == "launcher"; }

void ApplicationShell::ensureLauncher() {
    if (atLauncher())
        return;
    if (!atHome())
        (void)navigation_.resetTo("home");
    (void)navigation_.push("launcher");
    launcher_.activate();
}

void ApplicationShell::restoreLauncherFromMiniApp(bool showUnavailableReason) {
    showingMiniApp_ = false;
    display_.beginTransition(SlideDirection::Backward);
    if (miniApps_.hasActiveApp())
        (void)miniApps_.deactivate();
    ensureLauncher();
    launcher_.activate();
    if (showUnavailableReason)
        launcher_.showUnavailableOverlay();
}

void ApplicationShell::finishMiniAppUpdate(bool missingCapability) {
    if (miniApps_.hasActiveApp()) {
        showingMiniApp_ = true;
        return;
    }
    restoreLauncherFromMiniApp(missingCapability);
}

void ApplicationShell::applyMiniAppUpdate(const InputEvents& input,
                                          std::chrono::milliseconds elapsed) {
    miniAppUpdateInProgress_ = true;
    const auto result = miniApps_.update(input, elapsed);
    miniAppUpdateInProgress_ = false;
    if (miniAppCloseRequested_) {
        miniAppCloseRequested_ = false;
        if (miniApps_.hasActiveApp())
            (void)miniApps_.deactivate();
        restoreLauncherFromMiniApp(false);
        return;
    }
    finishMiniAppUpdate(result == MiniAppUpdateResult::DeactivatedMissingCapability);
}

void ApplicationShell::playInputFeedback(const InputEvent& event) {
    const bool adjustable = atSettings() && isUnmodified(event) &&
                            settingsSelection_ >= volumeRow && (isLeft(event) || isRight(event));
    if (adjustable) {
        const bool right = isRight(event);
        const char* id = "audio.volume.step";
        int value = audio_.volume(), minimum = 0, maximum = 100, step = 10;
        if (settingsSelection_ == timeoutRow) {
            id = "display.timeout.step";
            value = static_cast<int>(deviceSettings_.screenTimeout());
            maximum = 2;
            step = 1;
        } else if (settingsSelection_ == screenBrightnessRow) {
            id = "display.brightness.step";
            value = deviceSettings_.screenBrightness();
            minimum = 20;
        } else if (settingsSelection_ == ledBrightnessRow) {
            id = "indicator.brightness.step";
            value = deviceSettings_.ledBrightness();
            minimum = 1;
            maximum = 10;
            step = 1;
        }
        const bool changesValue = right ? value < maximum : value > minimum;
        const auto result =
            changesValue
                ? actions_.dispatch({id,
                                     "settings",
                                     {{"delta", static_cast<std::int32_t>(right ? step : -step)}}})
                : DispatchResult::Rejected;
        if (result == DispatchResult::Handled && changesValue) {
            (void)audio_.play(right ? services::AudioCue::StepRight : services::AudioCue::StepLeft);
        } else {
            (void)audio_.play(services::AudioCue::KeyPress);
        }
        return;
    }
    (void)audio_.play(services::AudioCue::KeyPress);
}

void ApplicationShell::routeMiniAppEvent(const InputEvent& event) {
    if (isPlainEscape(event)) {
        (void)actions_.dispatch({"app.close", "shell", {}});
        restoreLauncherFromMiniApp(false);
        return;
    }
    applyMiniAppUpdate({event}, {});
}

void ApplicationShell::routeSystemEvent(const InputEvent& event) {
    const bool plain = isUnmodified(event);
    const bool settingsChord = plain && !event.modifiers.shift && !event.modifiers.fn &&
                               event.type == InputEventType::NamedKey &&
                               event.namedKey == NamedKey::Tab &&
                               (atHome() || atSettings() || atBluetooth() || atWifi());
    const bool homeEnter = atHome() && plain && !event.modifiers.shift && !event.modifiers.fn &&
                           event.type == InputEventType::NamedKey &&
                           event.namedKey == NamedKey::Enter;
    if (atWifi() && wifiSettings_.modal()) {
        wifiSettings_.update({event});
    } else if (settingsChord) {
        (void)actions_.dispatch({"ui.settings", "shell", {}});
    } else if (homeEnter) {
        (void)actions_.dispatch({"ui.launcher", "shell", {}});
    } else if (atLauncher()) {
        launcher_.update({event});
    } else if (atWifi()) {
        wifiSettings_.update({event});
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
            const auto nextSelection = static_cast<std::uint8_t>(
                down ? std::min<int>(settingsSelection_ + 1, settingsRowCount - 1)
                     : std::max<int>(settingsSelection_ - 1, 0));
            if (nextSelection != settingsSelection_)
                settingsSelection_ = nextSelection;
        } else if (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Enter) {
            if (settingsSelection_ == bluetoothRow)
                (void)actions_.dispatch({"ui.bluetooth", "settings", {}});
            else if (settingsSelection_ == wifiRow)
                (void)actions_.dispatch({"ui.wifi", "settings", {}});
        } else if (isPlainEscape(event))
            (void)actions_.dispatch({"ui.back", "settings", {}});
    }
}

void ApplicationShell::tickCurrentPresentation(std::chrono::milliseconds elapsed,
                                               std::optional<std::uint8_t> batteryPercent,
                                               bool displayOff) {
    if (showingMiniApp_ && !miniApps_.hasActiveApp())
        restoreLauncherFromMiniApp(false);
    if (miniApps_.hasActiveApp()) {
        applyMiniAppUpdate({}, elapsed);
        if (miniApps_.hasActiveApp())
            return;
    }
    if (atHome())
        // A dark backlight holds the ambient wave's phase instead of advancing it.
        renderHome(display_.transitionActive() || displayOff ? std::chrono::milliseconds(0)
                                                             : elapsed,
                   batteryPercent);
    else if (atSettings())
        renderSettings();
    else if (atWifi())
        wifiSettings_.update({}, elapsed);
    else if (atLauncher())
        launcher_.update({}, elapsed);
    else
        settings_.update({});
}

ActionHandlingResult ApplicationShell::handle(const Action& action) {
    if (action.id == "ui.settings") {
        if ((atBluetooth() && settings_.modal()) || (atWifi() && wifiSettings_.modal()))
            return ActionHandlingResult::Rejected;
        if (atHome()) {
            display_.beginTransition(SlideDirection::Forward);
            (void)navigation_.push("settings");
            settingsFrame_.reset();
        } else if (atBluetooth() || atWifi()) {
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
    } else if (action.id == "ui.wifi") {
        if (!atSettings())
            return ActionHandlingResult::Rejected;
        display_.beginTransition(SlideDirection::Forward);
        (void)navigation_.push("wifi");
        wifiSettings_.activate();
    } else if (action.id == "ui.back") {
        if (atBluetooth() && settings_.modal()) {
            (void)actions_.dispatch({"hosts.back", "shell", {}});
        } else if (atWifi() && wifiSettings_.modal()) {
            wifiSettings_.update({{InputEventType::NamedKey, 0, NamedKey::Escape, {}}});
        } else if (atWifi()) {
            display_.beginTransition(SlideDirection::Backward);
            (void)navigation_.back();
            settingsFrame_.reset();
        } else if (atLauncher()) {
            display_.beginTransition(SlideDirection::Backward);
            launcher_.deactivate();
            (void)navigation_.back();
            homeConnectionFrame_.reset();
            homeNetworkFrame_.reset();
        } else if (!atHome()) {
            display_.beginTransition(SlideDirection::Backward);
            // Preserve the existing BLE list's explicit Esc Home behavior.
            (void)navigation_.resetTo("home");
            homeConnectionFrame_.reset();
            homeNetworkFrame_.reset();
        }
    } else if (action.id == "ui.launcher") {
        if (!atHome())
            return ActionHandlingResult::Rejected;
        display_.beginTransition(SlideDirection::Forward);
        ensureLauncher();
    } else if (action.id == "app.open") {
        const auto* value = action.findParameter("appId");
        const auto* appId = value ? std::get_if<std::string>(value) : nullptr;
        if (appId == nullptr || appId->empty())
            return ActionHandlingResult::Rejected;
        if (!atLauncher() || miniApps_.hasActiveApp())
            return ActionHandlingResult::Rejected;
        const auto result = miniApps_.activate(*appId);
        if (result == MiniAppActivationResult::AlreadyActive)
            return ActionHandlingResult::Handled;
        if (result != MiniAppActivationResult::Activated)
            return ActionHandlingResult::Rejected;
        showingMiniApp_ = true;
        launcher_.deactivate();
        display_.beginTransition(SlideDirection::Forward);
    } else if (action.id == "app.close") {
        if (!miniApps_.hasActiveApp())
            return ActionHandlingResult::Rejected;
        if (miniAppUpdateInProgress_) {
            miniAppCloseRequested_ = true;
            return ActionHandlingResult::Handled;
        }
        (void)miniApps_.deactivate();
    } else
        return ActionHandlingResult::Rejected;
    return ActionHandlingResult::Handled;
}

void ApplicationShell::update(const InputEvents& input, std::chrono::milliseconds elapsed,
                              std::optional<std::uint8_t> batteryPercent, bool displayOff) {
    display_.beginFrame();
    display_.advanceTransition(elapsed);
    if (showingMiniApp_ && !miniApps_.hasActiveApp())
        restoreLauncherFromMiniApp(false);
    for (const auto& event : input) {
        playInputFeedback(event);
        if (miniApps_.hasActiveApp())
            routeMiniAppEvent(event);
        else
            routeSystemEvent(event);
    }
    tickCurrentPresentation(elapsed, batteryPercent, displayOff);
    display_.endFrame();
}

void ApplicationShell::renderSettings() {
    const SettingsFrame next{settingsSelection_, audio_.volume(), deviceSettings_.screenTimeout(),
                             deviceSettings_.screenBrightness(), deviceSettings_.ledBrightness()};
    if (settingsFrame_ && settingsFrame_->selection == next.selection &&
        settingsFrame_->volume == next.volume && settingsFrame_->timeout == next.timeout &&
        settingsFrame_->screenBrightness == next.screenBrightness &&
        settingsFrame_->ledBrightness == next.ledBrightness)
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
            settingsFrame_ &&
            ((index == volumeRow && settingsFrame_->volume != next.volume) ||
             (index == timeoutRow && settingsFrame_->timeout != next.timeout) ||
             (index == screenBrightnessRow &&
              settingsFrame_->screenBrightness != next.screenBrightness) ||
             (index == ledBrightnessRow && settingsFrame_->ledBrightness != next.ledBrightness));
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
    drawRow(1, 42, "02", "Wi-Fi", {});
    drawRow(2, 60, "03", "Sound volume", std::to_string(audio_.volume()) + "%");
    const char* timeout = next.timeout == core::ScreenTimeoutMode::Normal ? "Normal"
                          : next.timeout == core::ScreenTimeoutMode::Long ? "Long"
                                                                          : "Never";
    drawRow(3, 78, "04", "Screen timeout", timeout);
    drawRow(4, 96, "05", "Screen brightness", std::to_string(next.screenBrightness) + "%");
    drawRow(5, 114, "06", "LED brightness", std::to_string(next.ledBrightness) + "%");
    settingsFrame_ = next;
}

void ApplicationShell::renderHome(std::chrono::milliseconds elapsed,
                                  std::optional<std::uint8_t> batteryPercent) {
    const auto hostStatus = hosts_.status();
    const auto networkStatus = network_.status();
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
    const bool companionReady = capabilities_.isAvailable("COMPANION");
    const bool entering = !homeConnectionFrame_;
    const bool connectionChanged = entering ||
                                   homeConnectionFrame_->activeHost != hostStatus.activeHostId ||
                                   homeConnectionFrame_->hostName != name ||
                                   homeConnectionFrame_->status != hostStatus.connection;
    const bool companionChanged =
        entering || homeConnectionFrame_->companionReady != companionReady;
    const HomeNetworkFrame nextNetwork{networkStatus.configured, networkStatus.enabled,
                                       networkStatus.connection};
    const bool networkChanged = !homeNetworkFrame_ ||
                                homeNetworkFrame_->configured != nextNetwork.configured ||
                                homeNetworkFrame_->enabled != nextNetwork.enabled ||
                                homeNetworkFrame_->connection != nextNetwork.connection;
    const TextStyle normal{palette::ink, palette::bone, 1};
    const TextStyle quiet{palette::ordinal, palette::bone, 1};
    if (entering) {
        display_.clear(palette::bone);
        display_.drawText({8, 8}, "--:--", normal);
        display_.fillRectangle({8, 24}, 224, 1, palette::ink);
    }
    if (networkChanged) {
        display_.fillRectangle(homeWifiRegion, homeWifiRegionWidth, homeWifiRegionHeight,
                               palette::bone);
        drawWifiIcon(display_, homeWifiIconPosition, palette::ink);
        drawWifiStatusIndicator(display_, homeWifiDotPosition, homeWifiIndicator(networkStatus));
        homeNetworkFrame_ = nextNetwork;
    }
    if (connectionChanged) {
        display_.fillRectangle({8, 32}, 224, 65, palette::bone);
        display_.drawText({8, 38}, "SELECTED HOST", quiet);
        drawHomeHostName(display_, name);
        drawBluetoothIcon(display_, accent);
        display_.drawText({29, 84}, state, normal);
        homeConnectionFrame_ = HomeConnectionFrame{hostStatus.activeHostId, name,
                                                   hostStatus.connection, companionReady};
        drawCompanionIndicator(display_, companionReady, name);
    } else if (companionChanged) {
        drawCompanionIndicator(display_, companionReady, name);
        homeConnectionFrame_->companionReady = companionReady;
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
