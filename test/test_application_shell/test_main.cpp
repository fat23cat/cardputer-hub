#include "apps/hosts/host_settings.h"
#include "apps/network/wifi_settings.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/application_shell.h"
#include "apps/shell/home_ambient.h"
#include "apps/shell/home_graphics.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "core/lifecycle/system_runtime.h"
#include "services/audio/audio_service.h"
#include "services/network/network_service.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <unity.h>
#include <vector>

using namespace cardputer_hub;

namespace {

class Memory final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {core::StorageReadStatus::NotFound, {}};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes&) override {
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
};

class Backlight final : public core::IBacklightAdapter {
  public:
    std::uint8_t level() const override { return brightness; }
    void setLevel(std::uint8_t value) override { brightness = value; }
    std::uint8_t brightness = 255;
};

class Led final : public core::ILEDAdapter {
  public:
    void writeFrame(const core::LedHardwareFrame&) override {}
};

class RuntimePlatform final : public core::IPlatformAdapter {
  public:
    void begin() override {}
    void update() override {}
};

class RuntimeKeyboard final : public core::IKeyboardAdapter {
  public:
    core::KeyboardPollResult poll(core::InputEvents& events) override {
        events = next;
        next.clear();
        const bool wasPressed = physicalPress;
        physicalPress = false;
        return {wasPressed};
    }
    void press(const core::InputEvent& event) {
        next = {event};
        physicalPress = true;
    }
    core::InputEvents next;
    bool physicalPress = false;
};

class SilentLogSink final : public core::ILogSink {
  public:
    void write(const core::LogRecord&) override {}
};

class Display final : public core::IDisplayAdapter {
  public:
    void beginFrame() override { dirty = false; }
    void endFrame() override {
        if (dirty)
            ++presentations;
    }
    void beginTransition(core::SlideDirection direction) override {
        transitions.push_back(direction);
        if (emulateTransitions)
            transitionRemainingMilliseconds = 220;
    }
    void advanceTransition(std::chrono::milliseconds elapsed) override {
        if (emulateTransitions)
            transitionRemainingMilliseconds = std::max<std::int64_t>(
                0, transitionRemainingMilliseconds - std::max<std::int64_t>(0, elapsed.count()));
    }
    bool transitionActive() const override {
        return emulateTransitions && transitionRemainingMilliseconds > 0;
    }
    void clear(core::RgbColor) override {
        ++frames;
        texts.clear();
        dirty = true;
    }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        fills.push_back({position, width, height, color});
        dirty = true;
    }
    bool drewCompanionDiamond() const {
        const auto origin = apps::homeCompanionPosition;
        return std::any_of(fills.begin(), fills.end(), [origin](const Fill& fill) {
            return fill.color.red == core::palette::leaf.red &&
                   fill.color.green == core::palette::leaf.green &&
                   fill.color.blue == core::palette::leaf.blue && fill.position.x >= origin.x &&
                   fill.position.y >= origin.y &&
                   fill.position.x < origin.x + apps::homeCompanionIndicatorSize &&
                   fill.position.y < origin.y + apps::homeCompanionIndicatorSize;
        });
    }
    struct Fill {
        core::PixelPosition position{};
        std::int32_t width = 0;
        std::int32_t height = 0;
        core::RgbColor color{};
    };
    std::vector<Fill> fills;
    void drawText(core::PixelPosition position, const char* value, core::TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        TEST_ASSERT_TRUE(position.x + std::string(value).size() * 6 * style.scale <= 240);
        TEST_ASSERT_TRUE(position.y + 8 * style.scale <= 135);
        texts.push_back(value);
        dirty = true;
    }
    bool shows(const char* value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
    std::vector<std::string> texts;
    std::vector<core::SlideDirection> transitions;
    int frames = 0;
    int presentations = 0;
    bool dirty = false;
    bool emulateTransitions = false;
    std::int64_t transitionRemainingMilliseconds = 0;
};

class AudioAdapter final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t) override { return true; }
    void setVolume(std::uint8_t) override {}
    bool isPlaying() const override { return false; }
    bool play(const core::AudioClip& clip) override {
        clips.push_back(clip);
        return true;
    }
    std::vector<core::AudioClip> clips;
};

class BluetoothAdapter final : public connectivity::IBluetoothAdapter {
  public:
    connectivity::BluetoothAdapterResult initialize(const connectivity::BluetoothDeviceConfig&,
                                                    std::uint32_t) override {
        return {};
    }
    connectivity::BluetoothAdapterResult shutdown() override { return {}; }
    connectivity::BluetoothAdvertisingResult
    startAdvertising(std::uint32_t, std::optional<connectivity::BluetoothBondReference>) override {
        return {};
    }
    connectivity::BluetoothAdapterResult requestAdvertisingStop() override { return {}; }
    connectivity::BluetoothAdapterResult
    disconnectPeer(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothPollResult pollEvent() override { return {}; }
    connectivity::BluetoothBondQueryResult bondState(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult beginPairing(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    restoreBondSecurity(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    respondToPairing(connectivity::BluetoothPeerHandle, connectivity::BluetoothPairingChallengeType,
                     bool, std::optional<std::uint32_t>) override {
        return {};
    }
    connectivity::BluetoothBondListResult bonds() override { return {}; }
    connectivity::BluetoothBondReferenceResult
    bondReference(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    deleteBond(const connectivity::BluetoothBondReference&) override {
        return {};
    }
    connectivity::BluetoothAdapterResult
    deleteBondForPeer(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothHidAdapterResult
    hidReadiness(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothHidAdapterResult sendHidReport(connectivity::BluetoothPeerHandle,
                                                          const connectivity::HidReport&) override {
        return {};
    }
    connectivity::BluetoothHidAdapterResult
    releaseHidReports(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    connectivity::BluetoothCompanionAdapterResult
    companionReadiness(connectivity::BluetoothPeerHandle) override {
        return connectivity::BluetoothCompanionAdapterResult::NotReady;
    }
    connectivity::BluetoothCompanionAdapterResult
    sendCompanionChunk(connectivity::BluetoothPeerHandle, const std::uint8_t*,
                       std::size_t) override {
        return connectivity::BluetoothCompanionAdapterResult::NotReady;
    }
    bool receiveCompanionChunk(connectivity::CompanionChunk&) override { return false; }
    bool takeCompanionIncomingOverflow() override { return false; }
};

class WifiAdapter final : public connectivity::IWifiAdapter {
  public:
    connectivity::WifiAdapterResult initializeStation() override { return {}; }
    connectivity::WifiAdapterResult connect(const connectivity::WifiNetworkConfig&) override {
        return {};
    }
    connectivity::WifiAdapterResult disconnect() override { return {}; }
    connectivity::WifiAdapterState state() const override {
        return connectivity::WifiAdapterState::Disconnected;
    }
    std::optional<std::int32_t> signalStrengthDbm() const override { return std::nullopt; }
};

class FakeMiniApp final : public apps::IMiniApp {
  public:
    void onActivate() override { ++activateCount; }
    void onDeactivate() override {
        if (insideUpdate)
            deactivatedDuringUpdate = true;
        ++deactivateCount;
    }
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override {
        insideUpdate = true;
        ++updateCount;
        lastInput = input;
        lastElapsed = elapsed;
        received.insert(received.end(), input.begin(), input.end());
        elapsedValues.push_back(elapsed);
        totalElapsed += elapsed;
        if (closeOnEnter && bus != nullptr) {
            for (const auto& event : input) {
                if (event.type == core::InputEventType::NamedKey &&
                    event.namedKey == core::NamedKey::Enter) {
                    (void)bus->dispatch({"app.close", "mini-app", {}});
                    break;
                }
            }
        }
        if (closeOnEmptyUpdate && input.empty() && bus != nullptr)
            (void)bus->dispatch({"app.close", "mini-app", {}});
        if (deactivateCount != 0)
            deactivatedDuringUpdate = true;
        if (display != nullptr)
            display->texts = {"MINI APP"};
        insideUpdate = false;
    }
    Display* display = nullptr;
    core::ActionBus* bus = nullptr;
    bool closeOnEnter = false;
    bool closeOnEmptyUpdate = false;
    bool insideUpdate = false;
    bool deactivatedDuringUpdate = false;
    int activateCount = 0;
    int deactivateCount = 0;
    int updateCount = 0;
    core::InputEvents lastInput;
    core::InputEvents received;
    std::vector<std::chrono::milliseconds> elapsedValues;
    std::chrono::milliseconds lastElapsed{0};
    std::chrono::milliseconds totalElapsed{0};
};

class DrawingMiniApp final : public apps::IMiniApp {
  public:
    explicit DrawingMiniApp(Display& display) : display(&display) {}
    void onActivate() override { ++activateCount; }
    void onDeactivate() override { ++deactivateCount; }
    void update(const core::InputEvents&, std::chrono::milliseconds) override {
        ++updateCount;
        display->clear(core::palette::bone);
        display->drawText({6, 6}, "SYSTEM", {core::palette::ink, core::palette::bone, 1});
        display->drawText({10, 25}, "MINI APP SCREEN",
                          {core::palette::ink, core::palette::bone, 1});
    }
    Display* display = nullptr;
    int activateCount = 0;
    int deactivateCount = 0;
    int updateCount = 0;
};

struct Fixture {
    Memory memory;
    core::Storage storage{memory};
    services::ConfigurationService config{storage};
    AudioAdapter audioAdapter;
    services::AudioService audio{config, audioAdapter};
    Backlight backlight;
    core::DisplayPowerController displayPower{backlight};
    Led led;
    services::IndicatorService indicator{led};
    services::DeviceSettingsService deviceSettings{config, displayPower, indicator};
    BluetoothAdapter bluetoothAdapter;
    connectivity::BluetoothService bluetooth{bluetoothAdapter};
    services::HostService hosts{bluetooth, config};
    WifiAdapter wifiAdapter;
    connectivity::WiFiService wifi{wifiAdapter};
    services::NetworkService network{wifi, config};
    core::ActionBus bus;
    Display display;
    apps::HostSettings hostSettings{hosts, bus, display};
    apps::WiFiSettings wifiSettings{network, bus, display};
    core::AppRegistry appRegistry;
    core::CapabilityRegistry capabilities;
    apps::MiniAppRuntime miniApps{appRegistry, capabilities};
    FakeMiniApp app;
    Fixture() {
        app.display = &display;
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(audio.start() == services::AudioResult::Success);
        displayPower.captureNormalLevel();
        TEST_ASSERT_TRUE(deviceSettings.start());
        TEST_ASSERT_TRUE(bus.registerHandler("audio.volume.step", audio) ==
                         core::RegistrationResult::Registered);
        for (const auto* id :
             {"display.timeout.step", "display.brightness.step", "indicator.brightness.step"})
            TEST_ASSERT_TRUE(bus.registerHandler(id, deviceSettings) ==
                             core::RegistrationResult::Registered);
        for (const auto* id : {"network.set-enabled", "network.configure", "network.forget"})
            TEST_ASSERT_TRUE(bus.registerHandler(id, network) ==
                             core::RegistrationResult::Registered);
    }
    apps::ApplicationShell makeShell() {
        return apps::ApplicationShell(hosts, network, bus, display, hostSettings, wifiSettings,
                                      audio, deviceSettings, miniApps, capabilities);
    }
    void registerApp(const char* id, apps::IMiniApp& instance,
                     std::vector<std::string> required = {}) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::AppRegistrationResult::Registered),
                                static_cast<unsigned int>(appRegistry.registerApp(
                                    {id, id, "", std::string(id) + "/home", std::move(required)})));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned int>(apps::MiniAppInstanceRegistrationResult::Registered),
            static_cast<unsigned int>(miniApps.registerInstance(id, instance)));
    }
    void registerApp(const char* id, std::vector<std::string> required = {}) {
        registerApp(id, app, std::move(required));
    }
};

struct RuntimeBridge {
    explicit RuntimeBridge(Fixture& fixture)
        : runtime(platform, keyboard, fixture.display, fixture.displayPower, logger, buildInfo) {
        runtime.start();
        (void)runtime.update(std::chrono::milliseconds(2000));
    }
    void idleOff() {
        (void)runtime.update(core::DisplayPowerController::idleThreshold +
                             core::DisplayPowerController::dimRampDuration +
                             core::DisplayPowerController::dimHoldDuration +
                             core::DisplayPowerController::offRampDuration);
        TEST_ASSERT_TRUE(runtime.displayOff());
    }
    const core::InputEvents& press(const core::InputEvent& event) {
        keyboard.press(event);
        return runtime.update(std::chrono::milliseconds(1));
    }
    RuntimePlatform platform;
    RuntimeKeyboard keyboard;
    SilentLogSink sink;
    core::Logger logger{sink, core::LogLevel::Info};
    const core::BuildInfo buildInfo{"Test Hub", "1", "test", "test"};
    core::SystemRuntime runtime;
};

const core::InputEvent tab{core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {}};

const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};
const core::InputEvent left{core::InputEventType::NamedKey, 0, core::NamedKey::Left, {}};
const core::InputEvent right{core::InputEventType::NamedKey, 0, core::NamedKey::Right, {}};
const core::InputEvent keyA{core::InputEventType::PrintableCharacter, 'A', {}, {}};
const core::InputEvent keyB{core::InputEventType::PrintableCharacter, 'B', {}, {}};
const core::InputEvent keyC{core::InputEventType::PrintableCharacter, 'C', {}, {}};

bool isPrintable(const core::InputEvent& event, char value) {
    return event.type == core::InputEventType::PrintableCharacter && event.character == value;
}

std::size_t keyPressCount(const std::vector<core::AudioClip>& clips) {
    return static_cast<std::size_t>(
        std::count_if(clips.begin(), clips.end(), [](const core::AudioClip& clip) {
            return clip.sampleCount == services::AudioService::keyClipLength;
        }));
}

std::size_t stepCount(const std::vector<core::AudioClip>& clips) {
    return static_cast<std::size_t>(
        std::count_if(clips.begin(), clips.end(), [](const core::AudioClip& clip) {
            return clip.sampleCount == services::AudioService::stepClipLength;
        }));
}

void test_idle_runtime_preserves_home_and_settings() {
    Fixture f;
    auto shell = f.makeShell();

    shell.update({});
    TEST_ASSERT_TRUE(f.display.shows("WiFi"));
    TEST_ASSERT_TRUE(f.display.shows("BT"));
    TEST_ASSERT_TRUE(f.display.shows("--%"));
    TEST_ASSERT_FALSE(f.display.shows("--:--"));
    TEST_ASSERT_FALSE(f.display.shows("SELECTED HOST"));
    TEST_ASSERT_FALSE(f.display.shows("OFF"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
    TEST_ASSERT_FALSE(f.display.shows("APPS"));

    shell.update({tab});
    TEST_ASSERT_TRUE(f.display.shows("SETTINGS"));
    TEST_ASSERT_TRUE(f.display.shows("Bluetooth"));
    TEST_ASSERT_TRUE(f.display.shows("Wi-Fi"));
    TEST_ASSERT_TRUE(f.display.shows("Sound volume"));
    TEST_ASSERT_EQUAL_INT(0, f.app.updateCount);
}

void test_home_enter_opens_launcher_and_tab_still_opens_settings() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();

    shell.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));

    shell.update({escape});
    TEST_ASSERT_TRUE(f.display.shows("BT"));
    shell.update({tab});
    TEST_ASSERT_TRUE(f.display.shows("SETTINGS"));
}

void test_active_mini_app_receives_scheduled_update_instead_of_shell_input() {
    Fixture f;
    f.registerApp("weather");
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned int>(f.miniApps.activate("weather")));
    auto shell = f.makeShell();

    shell.update({tab}, std::chrono::milliseconds(41));

    TEST_ASSERT_EQUAL_UINT32(1, f.app.received.size());
    TEST_ASSERT_TRUE(f.app.received.front().namedKey == core::NamedKey::Tab);
    TEST_ASSERT_EQUAL_UINT32(2, f.app.elapsedValues.size());
    TEST_ASSERT_EQUAL_INT64(0, f.app.elapsedValues.front().count());
    TEST_ASSERT_EQUAL_INT64(41, f.app.elapsedValues.back().count());
    TEST_ASSERT_EQUAL_INT64(41, f.app.totalElapsed.count());
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
}

void test_explicit_runtime_deactivation_returns_shell_to_launcher() {
    Fixture f;
    f.registerApp("weather");
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned int>(f.miniApps.activate("weather")));
    auto shell = f.makeShell();
    shell.update({tab}, std::chrono::milliseconds(16));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppDeactivationResult::Deactivated),
                            static_cast<unsigned int>(f.miniApps.deactivate()));
    shell.update({tab});

    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
    TEST_ASSERT_EQUAL_INT(2, f.app.updateCount);
}

void test_explicit_runtime_deactivation_restores_launcher_before_idle_update() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({});
    TEST_ASSERT_TRUE(f.display.shows("WiFi"));
    f.registerApp("weather");
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned int>(f.miniApps.activate("weather")));
    shell.update({}, std::chrono::milliseconds(16));
    TEST_ASSERT_TRUE(f.display.shows("MINI APP"));
    TEST_ASSERT_FALSE(f.display.shows("WiFi"));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppDeactivationResult::Deactivated),
                            static_cast<unsigned int>(f.miniApps.deactivate()));
    shell.update({});

    TEST_ASSERT_FALSE(f.display.shows("MINI APP"));
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("weather"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
}

void test_capability_loss_returns_shell_to_launcher_without_forwarding_input() {
    Fixture f;
    f.registerApp("weather", {"WIFI"});
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(core::CapabilityRegistrationResult::Registered),
        static_cast<unsigned int>(f.capabilities.registerCapability("WIFI")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned int>(f.miniApps.activate("weather")));
    auto shell = f.makeShell();
    shell.update({}, std::chrono::milliseconds(16));
    TEST_ASSERT_EQUAL_INT(1, f.app.updateCount);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::CapabilityRemovalResult::Removed),
                            static_cast<unsigned int>(f.capabilities.removeCapability("WIFI")));
    shell.update({tab}, std::chrono::milliseconds(16));

    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.updateCount);
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
}

void test_launcher_enter_activates_and_escape_returns_to_launcher() {
    Fixture f;
    f.registerApp("weather");
    auto shell = f.makeShell();
    shell.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    shell.update({enter});
    TEST_ASSERT_EQUAL_INT(1, f.app.activateCount);
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    shell.update({escape});
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("weather"));
}

void test_drawing_mini_app_handoff_fully_redraws_launcher_backward() {
    Fixture f;
    DrawingMiniApp drawing(f.display);
    f.registerApp("system", drawing);
    auto shell = f.makeShell();

    shell.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
    TEST_ASSERT_TRUE(f.display.shows("1/1"));

    shell.update({enter});
    TEST_ASSERT_EQUAL_INT(1, drawing.activateCount);
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("SYSTEM"));
    TEST_ASSERT_TRUE(f.display.shows("MINI APP SCREEN"));
    TEST_ASSERT_FALSE(f.display.shows("APPS"));
    TEST_ASSERT_FALSE(f.display.transitions.empty());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Forward);

    shell.update({escape});
    TEST_ASSERT_EQUAL_INT(1, drawing.deactivateCount);
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_FALSE(f.display.shows("MINI APP SCREEN"));
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
    TEST_ASSERT_TRUE(f.display.shows("1/1"));
    TEST_ASSERT_FALSE(f.display.transitions.empty());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
}

void test_capability_loss_from_launcher_uses_backward_transition_and_overlay() {
    Fixture f;
    DrawingMiniApp drawing(f.display);
    f.registerApp("weather", drawing, {"WIFI"});
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(core::CapabilityRegistrationResult::Registered),
        static_cast<unsigned int>(f.capabilities.registerCapability("WIFI")));
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("MINI APP SCREEN"));
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::CapabilityRemovalResult::Removed),
                            static_cast<unsigned int>(f.capabilities.removeCapability("WIFI")));
    f.display.transitions.clear();
    shell.update({}, std::chrono::milliseconds(16));

    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, drawing.deactivateCount);
    TEST_ASSERT_FALSE(f.display.shows("MINI APP SCREEN"));
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("weather"));
    TEST_ASSERT_FALSE(f.display.transitions.empty());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);

    shell.update({}, std::chrono::milliseconds(120));
    TEST_ASSERT_TRUE(f.display.shows("REQUIRES WIFI"));
}

void test_launcher_enter_then_down_does_not_move_hidden_selection() {
    Fixture f;
    f.registerApp("system");
    f.registerApp("weather");
    auto shell = f.makeShell();
    shell.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("1/2"));

    shell.update({enter, down});
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.activateCount);

    shell.update({escape});
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("1/2"));
    TEST_ASSERT_FALSE(f.display.shows("2/2"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
}

void test_launcher_enter_then_escape_does_not_leave_launcher_route() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("APPS"));

    shell.update({enter, escape});
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.activateCount);
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
    TEST_ASSERT_FALSE(f.display.shows("SELECTED HOST"));
    TEST_ASSERT_FALSE(f.display.transitions.empty());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
}

void test_mini_app_receives_printable_events_in_order() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});

    shell.update({keyA, keyB, keyC});

    TEST_ASSERT_EQUAL_UINT32(3, f.app.received.size());
    TEST_ASSERT_TRUE(isPrintable(f.app.received[0], 'A'));
    TEST_ASSERT_TRUE(isPrintable(f.app.received[1], 'B'));
    TEST_ASSERT_TRUE(isPrintable(f.app.received[2], 'C'));
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
}

void test_input_before_escape_reaches_mini_app() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});

    shell.update({keyA, keyB, escape});

    TEST_ASSERT_EQUAL_UINT32(2, f.app.received.size());
    TEST_ASSERT_TRUE(isPrintable(f.app.received[0], 'A'));
    TEST_ASSERT_TRUE(isPrintable(f.app.received[1], 'B'));
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
}

void test_input_after_escape_is_routed_to_launcher() {
    Fixture f;
    f.registerApp("system");
    f.registerApp("weather");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());

    shell.update({escape, down});

    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("2/2"));
    TEST_ASSERT_TRUE(f.display.shows("weather"));
}

void test_events_on_both_sides_of_escape_change_owner() {
    Fixture f;
    f.registerApp("system");
    f.registerApp("weather");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});

    shell.update({keyA, escape, down});

    TEST_ASSERT_EQUAL_UINT32(1, f.app.received.size());
    TEST_ASSERT_TRUE(isPrintable(f.app.received.front(), 'A'));
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("2/2"));
    TEST_ASSERT_FALSE(f.display.shows("1/2"));
}

void test_app_initiated_close_routes_following_events_to_launcher() {
    Fixture f;
    f.app.bus = &f.bus;
    f.app.closeOnEnter = true;
    f.registerApp("system");
    f.registerApp("weather");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());

    shell.update({enter, down});

    TEST_ASSERT_EQUAL_UINT32(1, f.app.received.size());
    TEST_ASSERT_TRUE(f.app.received.front().namedKey == core::NamedKey::Enter);
    TEST_ASSERT_FALSE(f.app.deactivatedDuringUpdate);
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("2/2"));
}

void test_app_initiated_close_from_scheduled_tick_is_deferred() {
    Fixture f;
    f.app.bus = &f.bus;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    f.app.closeOnEmptyUpdate = true;

    shell.update({}, std::chrono::milliseconds(16));

    TEST_ASSERT_FALSE(f.app.deactivatedDuringUpdate);
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
}

void test_capability_loss_during_event_does_not_replay_into_launcher() {
    Fixture f;
    f.registerApp("weather", {"WIFI"});
    f.registerApp("system");
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(core::CapabilityRegistrationResult::Registered),
        static_cast<unsigned int>(f.capabilities.registerCapability("WIFI")));
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::CapabilityRemovalResult::Removed),
                            static_cast<unsigned int>(f.capabilities.removeCapability("WIFI")));

    shell.update({keyA, down}, std::chrono::milliseconds(16));

    TEST_ASSERT_TRUE(f.app.received.empty());
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.deactivateCount);
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_TRUE(f.display.shows("2/2"));
    TEST_ASSERT_TRUE(f.display.shows("system"));
    shell.update({}, std::chrono::milliseconds(120));
    TEST_ASSERT_TRUE(f.display.shows("REQUIRES WIFI"));
}

void test_mini_app_elapsed_advances_once_per_shell_update() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    f.app.elapsedValues.clear();
    f.app.totalElapsed = {};
    f.app.received.clear();

    shell.update({keyA, keyB, keyC}, std::chrono::milliseconds(16));

    TEST_ASSERT_EQUAL_UINT32(3, f.app.received.size());
    TEST_ASSERT_EQUAL_UINT32(4, f.app.elapsedValues.size());
    TEST_ASSERT_EQUAL_INT64(0, f.app.elapsedValues[0].count());
    TEST_ASSERT_EQUAL_INT64(0, f.app.elapsedValues[1].count());
    TEST_ASSERT_EQUAL_INT64(0, f.app.elapsedValues[2].count());
    TEST_ASSERT_EQUAL_INT64(16, f.app.elapsedValues[3].count());
    TEST_ASSERT_EQUAL_INT64(16, f.app.totalElapsed.count());
    TEST_ASSERT_TRUE(f.app.lastInput.empty());
}

void test_mini_app_printable_event_plays_one_keypress() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    f.audioAdapter.clips.clear();

    shell.update({keyA});

    TEST_ASSERT_EQUAL_UINT32(1, f.app.received.size());
    TEST_ASSERT_TRUE(isPrintable(f.app.received.front(), 'A'));
    TEST_ASSERT_EQUAL_UINT32(1, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT32(1, keyPressCount(f.audioAdapter.clips));
    TEST_ASSERT_EQUAL_UINT32(0, stepCount(f.audioAdapter.clips));
}

void test_mini_app_escape_plays_one_keypress_and_restores_launcher() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    f.audioAdapter.clips.clear();
    f.app.received.clear();

    shell.update({escape});

    TEST_ASSERT_TRUE(f.app.received.empty());
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_EQUAL_UINT32(1, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT32(1, keyPressCount(f.audioAdapter.clips));
}

void test_mini_app_batch_plays_one_cue_per_event() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    f.audioAdapter.clips.clear();

    shell.update({keyA, keyB, escape});

    TEST_ASSERT_EQUAL_UINT32(2, f.app.received.size());
    TEST_ASSERT_TRUE(isPrintable(f.app.received[0], 'A'));
    TEST_ASSERT_TRUE(isPrintable(f.app.received[1], 'B'));
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_TRUE(f.display.shows("APPS"));
    TEST_ASSERT_EQUAL_UINT32(3, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT32(3, keyPressCount(f.audioAdapter.clips));
    TEST_ASSERT_EQUAL_UINT32(0, stepCount(f.audioAdapter.clips));
}

void test_settings_volume_steps_play_directional_cues_without_keypress() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({tab});
    shell.update({down, down});
    f.audioAdapter.clips.clear();

    shell.update({right});
    TEST_ASSERT_EQUAL_UINT32(1, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT32(0, keyPressCount(f.audioAdapter.clips));
    TEST_ASSERT_EQUAL_UINT32(1, stepCount(f.audioAdapter.clips));
    const auto* rightSamples = f.audioAdapter.clips.back().samples;

    f.audioAdapter.clips.clear();
    shell.update({left});
    TEST_ASSERT_EQUAL_UINT32(1, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT32(0, keyPressCount(f.audioAdapter.clips));
    TEST_ASSERT_EQUAL_UINT32(1, stepCount(f.audioAdapter.clips));
    TEST_ASSERT_TRUE(f.audioAdapter.clips.back().samples != rightSamples);
}

void test_launcher_navigation_plays_one_keypress_per_event() {
    Fixture f;
    f.registerApp("system");
    f.registerApp("weather");
    auto shell = f.makeShell();
    shell.update({enter});
    f.audioAdapter.clips.clear();

    shell.update({down});

    TEST_ASSERT_TRUE(f.display.shows("2/2"));
    TEST_ASSERT_EQUAL_UINT32(1, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT32(1, keyPressCount(f.audioAdapter.clips));
    TEST_ASSERT_EQUAL_UINT32(0, stepCount(f.audioAdapter.clips));
}

void test_malformed_app_open_does_not_mutate_runtime() {
    Fixture f;
    f.registerApp("weather");
    auto shell = f.makeShell();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::ActionHandlingResult::Rejected),
                            static_cast<unsigned int>(shell.handle({"app.open", "test", {}})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(core::ActionHandlingResult::Rejected),
        static_cast<unsigned int>(shell.handle({"app.open", "test", {{"appId", 1}}})));
    TEST_ASSERT_FALSE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(0, f.app.activateCount);
}

void test_app_open_from_launcher_activates_when_idle() {
    Fixture f;
    f.registerApp("system");
    auto shell = f.makeShell();
    shell.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("APPS"));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::ActionHandlingResult::Handled),
                            static_cast<unsigned int>(shell.handle(
                                {"app.open", "test", {{"appId", std::string("system")}}})));
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.activateCount);
}

void test_app_open_is_rejected_while_mini_app_is_active() {
    Fixture f;
    FakeMiniApp weather;
    f.registerApp("system");
    f.registerApp("weather", weather);
    auto shell = f.makeShell();
    shell.update({enter});
    shell.update({enter});
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.activateCount);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::ActionHandlingResult::Rejected),
                            static_cast<unsigned int>(shell.handle(
                                {"app.open", "test", {{"appId", std::string("weather")}}})));
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
    TEST_ASSERT_EQUAL_INT(1, f.app.activateCount);
    TEST_ASSERT_EQUAL_INT(0, f.app.deactivateCount);
    TEST_ASSERT_EQUAL_INT(0, weather.activateCount);
    TEST_ASSERT_EQUAL_INT(0, weather.deactivateCount);
    TEST_ASSERT_TRUE(f.display.shows("MINI APP"));
    TEST_ASSERT_FALSE(f.display.shows("APPS"));
}

int countWaveFills(const Display& display) {
    return static_cast<int>(
        std::count_if(display.fills.begin(), display.fills.end(), [](const Display::Fill& fill) {
            return fill.color.red == core::palette::homeWave.red &&
                   fill.color.green == core::palette::homeWave.green &&
                   fill.color.blue == core::palette::homeWave.blue;
        }));
}

std::vector<Display::Fill> ambientFills(const Display& display) {
    std::vector<Display::Fill> result;
    for (const auto& fill : display.fills)
        if (fill.position.y >= apps::homeAmbientOrigin.y)
            result.push_back(fill);
    return result;
}

int countAmbientViewportClears(const Display& display) {
    return static_cast<int>(
        std::count_if(display.fills.begin(), display.fills.end(), [](const Display::Fill& fill) {
            return fill.position.x == apps::homeAmbientOrigin.x &&
                   fill.position.y == apps::homeAmbientOrigin.y &&
                   fill.width == apps::homeAmbientWidth && fill.height == apps::homeAmbientHeight;
        }));
}

void assertAmbientMatchesPhase(const Display& display, std::uint64_t phaseMilliseconds) {
    Display expected;
    apps::drawHomeAmbient(expected, phaseMilliseconds);
    const auto actual = ambientFills(display);
    TEST_ASSERT_TRUE(actual.size() >= expected.fills.size());
    const auto offset = actual.size() - expected.fills.size();
    for (std::size_t i = 0; i < expected.fills.size(); ++i) {
        TEST_ASSERT_EQUAL_INT(expected.fills[i].position.x, actual[offset + i].position.x);
        TEST_ASSERT_EQUAL_INT(expected.fills[i].position.y, actual[offset + i].position.y);
        TEST_ASSERT_EQUAL_INT(expected.fills[i].width, actual[offset + i].width);
        TEST_ASSERT_EQUAL_INT(expected.fills[i].height, actual[offset + i].height);
        TEST_ASSERT_EQUAL_UINT8(expected.fills[i].color.red, actual[offset + i].color.red);
        TEST_ASSERT_EQUAL_UINT8(expected.fills[i].color.green, actual[offset + i].color.green);
        TEST_ASSERT_EQUAL_UINT8(expected.fills[i].color.blue, actual[offset + i].color.blue);
    }
}

void test_home_orb_does_not_advance_while_display_off() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({});
    TEST_ASSERT_TRUE(countWaveFills(f.display) > 0);
    f.display.fills.clear();

    shell.update({}, std::chrono::milliseconds(600), std::nullopt, true);
    TEST_ASSERT_EQUAL_INT(0, countWaveFills(f.display));
    shell.update({}, std::chrono::milliseconds(600), std::nullopt, true);
    TEST_ASSERT_EQUAL_INT(0, countWaveFills(f.display));

    shell.update({}, std::chrono::milliseconds(600), std::nullopt, false);
    assertAmbientMatchesPhase(f.display, 600);
}

void test_home_entering_while_display_off_defers_ambient_frame() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({}, std::chrono::milliseconds(75), 42, true);
    TEST_ASSERT_TRUE(f.display.shows("WiFi"));
    TEST_ASSERT_TRUE(f.display.shows("BT"));
    TEST_ASSERT_TRUE(f.display.shows("42%"));
    TEST_ASSERT_TRUE(ambientFills(f.display).empty());

    f.display.fills.clear();
    shell.update({}, std::chrono::milliseconds(500), 42, true);
    TEST_ASSERT_TRUE(ambientFills(f.display).empty());
    shell.update({}, std::chrono::milliseconds(0), 42, false);
    assertAmbientMatchesPhase(f.display, 0);

    f.display.fills.clear();
    shell.update({}, std::chrono::milliseconds(49), 42, false);
    TEST_ASSERT_TRUE(ambientFills(f.display).empty());
    shell.update({}, std::chrono::milliseconds(1), 42, false);
    assertAmbientMatchesPhase(f.display, 50);

    shell.update({enter});
    f.display.fills.clear();
    shell.update({escape}, std::chrono::milliseconds(0), 42, true);
    TEST_ASSERT_EQUAL_INT(0, countAmbientViewportClears(f.display));
    f.display.fills.clear();
    shell.update({}, std::chrono::milliseconds(0), 42, false);
    assertAmbientMatchesPhase(f.display, 50);
}

void test_home_transition_freezes_ambient_phase_and_frame_interval() {
    Fixture f;
    f.display.emulateTransitions = true;
    auto shell = f.makeShell();
    shell.update({});
    f.display.fills.clear();
    shell.update({}, std::chrono::milliseconds(50));
    assertAmbientMatchesPhase(f.display, 50);

    f.display.fills.clear();
    shell.update({enter});
    shell.update({}, std::chrono::milliseconds(220));

    f.display.fills.clear();
    shell.update({escape});
    // The incoming Home page is composed once from the retained phase for the slide.
    assertAmbientMatchesPhase(f.display, 50);
    f.display.fills.clear();
    shell.update({}, std::chrono::milliseconds(100));
    shell.update({}, std::chrono::milliseconds(100));
    shell.update({}, std::chrono::milliseconds(20));
    TEST_ASSERT_TRUE(ambientFills(f.display).empty());
    shell.update({}, std::chrono::milliseconds(49));
    TEST_ASSERT_TRUE(ambientFills(f.display).empty());
    shell.update({}, std::chrono::milliseconds(1));
    assertAmbientMatchesPhase(f.display, 100);
}

void test_home_ambient_model_is_deterministic_and_bounded() {
    const std::array<std::uint64_t, 7> phases{0, 2000, 4000, 8000, 12000, 17000, 21000};
    double minimumRadius = 1000;
    double maximumRadius = 0;
    for (const auto phase : phases) {
        const auto frame = apps::homeAmbientFrame(phase);
        const auto again = apps::homeAmbientFrame(phase);
        TEST_ASSERT_EQUAL_UINT(40, frame.size());
        int large = 0;
        double centerX = 0;
        double centerY = 0;
        for (std::size_t i = 0; i < frame.size(); ++i) {
            const auto& particle = frame[i];
            TEST_ASSERT_EQUAL_INT(particle.position.x, again[i].position.x);
            TEST_ASSERT_EQUAL_INT(particle.position.y, again[i].position.y);
            TEST_ASSERT_EQUAL_UINT8(particle.size, again[i].size);
            TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(particle.tone),
                                    static_cast<unsigned>(again[i].tone));
            TEST_ASSERT_TRUE(particle.size >= 1 && particle.size <= 3);
            TEST_ASSERT_TRUE(particle.tone == apps::HomeAmbientTone::Far ||
                             particle.tone == apps::HomeAmbientTone::Middle ||
                             particle.tone == apps::HomeAmbientTone::Near);
            const int left = particle.position.x - particle.size / 2;
            const int top = particle.position.y - particle.size / 2;
            TEST_ASSERT_TRUE(left >= apps::homeAmbientOrigin.x);
            TEST_ASSERT_TRUE(top >= apps::homeAmbientOrigin.y);
            TEST_ASSERT_TRUE(left + particle.size <=
                             apps::homeAmbientOrigin.x + apps::homeAmbientWidth);
            TEST_ASSERT_TRUE(top + particle.size <=
                             apps::homeAmbientOrigin.y + apps::homeAmbientHeight);
            large += particle.size == 3;
            if (i < 28) {
                centerX += particle.position.x;
                centerY += particle.position.y;
            }
        }
        TEST_ASSERT_TRUE(large < 10);
        centerX /= 28;
        centerY /= 28;
        TEST_ASSERT_TRUE(std::abs(centerX - 120) <= 7);
        TEST_ASSERT_TRUE(std::abs(centerY - 79) <= 5);
        double averageRadius = 0;
        for (std::size_t i = 0; i < 28; ++i)
            averageRadius +=
                std::hypot(frame[i].position.x - centerX, frame[i].position.y - centerY);
        averageRadius /= 28;
        minimumRadius = std::min(minimumRadius, averageRadius);
        maximumRadius = std::max(maximumRadius, averageRadius);
    }
    TEST_ASSERT_TRUE(maximumRadius - minimumRadius > 2);
    const auto first = apps::homeAmbientFrame(phases.front());
    const auto later = apps::homeAmbientFrame(phases[1]);
    int changed = 0;
    for (std::size_t i = 0; i < first.size(); ++i)
        changed += first[i].position.x != later[i].position.x ||
                   first[i].position.y != later[i].position.y;
    TEST_ASSERT_TRUE(changed > 10);
}

void test_home_bluetooth_status_uses_semantic_dot_colors() {
    using apps::HomeStatusIndicator;
    using services::HostConnectionStatus;
    TEST_ASSERT_TRUE(apps::homeBluetoothIndicator(HostConnectionStatus::Off) ==
                     HomeStatusIndicator::HollowQuiet);
    for (auto status : {HostConnectionStatus::Connecting, HostConnectionStatus::Securing,
                        HostConnectionStatus::Pairing})
        TEST_ASSERT_TRUE(apps::homeBluetoothIndicator(status) == HomeStatusIndicator::FilledBlue);
    TEST_ASSERT_TRUE(apps::homeBluetoothIndicator(HostConnectionStatus::Ready) ==
                     HomeStatusIndicator::FilledLeaf);
    TEST_ASSERT_TRUE(apps::homeBluetoothIndicator(HostConnectionStatus::Error) ==
                     HomeStatusIndicator::FilledVermilion);
}

void test_home_ambient_throttles_and_delayed_update_renders_once() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({});
    const auto presentations = f.display.presentations;
    f.display.fills.clear();
    f.display.texts.clear();
    shell.update({}, std::chrono::milliseconds(49));
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.display.fills.empty());
    shell.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL(presentations + 1, f.display.presentations);
    TEST_ASSERT_EQUAL_INT(1, std::count_if(f.display.fills.begin(), f.display.fills.end(),
                                           [](const Display::Fill& fill) {
                                               return fill.position.x == 0 &&
                                                      fill.position.y ==
                                                          apps::homeAmbientOrigin.y &&
                                                      fill.width == apps::homeAmbientWidth &&
                                                      fill.height == apps::homeAmbientHeight;
                                           }));
    f.display.fills.clear();
    shell.update({}, std::chrono::milliseconds(250));
    TEST_ASSERT_EQUAL(presentations + 2, f.display.presentations);
    TEST_ASSERT_EQUAL_INT(1, std::count_if(f.display.fills.begin(), f.display.fills.end(),
                                           [](const Display::Fill& fill) {
                                               return fill.position.x == 0 &&
                                                      fill.position.y ==
                                                          apps::homeAmbientOrigin.y &&
                                                      fill.width == apps::homeAmbientWidth &&
                                                      fill.height == apps::homeAmbientHeight;
                                           }));
    for (const auto& fill : f.display.fills)
        TEST_ASSERT_TRUE(fill.position.y >= apps::homeAmbientOrigin.y);
    TEST_ASSERT_TRUE(f.display.texts.empty());
}

void test_home_reentry_starts_a_new_ambient_frame_interval() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({});
    shell.update({}, std::chrono::milliseconds(49));
    shell.update({enter});
    shell.update({escape});
    TEST_ASSERT_TRUE(f.display.shows("WiFi"));
    const auto presentations = f.display.presentations;
    f.display.fills.clear();

    shell.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.display.fills.empty());
    shell.update({}, std::chrono::milliseconds(48));
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    shell.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL(presentations + 1, f.display.presentations);
}

void test_wake_consumed_frame_plays_no_cue_and_dispatches_no_action() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({});
    TEST_ASSERT_TRUE(f.display.shows("WiFi"));
    f.audioAdapter.clips.clear();
    f.display.transitions.clear();

    // SystemRuntime already consumed the wake press, so this frame has no event.
    shell.update({}, std::chrono::milliseconds(16), std::nullopt, true);

    TEST_ASSERT_EQUAL_UINT(0, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT(0, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.shows("WiFi"));
    TEST_ASSERT_FALSE(f.display.shows("APPS"));

    shell.update({enter});

    TEST_ASSERT_TRUE(f.display.shows("APPS"));
}

void test_home_companion_indicator_appears_only_when_companion_capability_is_live() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({});
    TEST_ASSERT_FALSE(f.display.drewCompanionDiamond());
    TEST_ASSERT_FALSE(f.display.shows("COMPANION"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(core::CapabilityRegistrationResult::Registered),
                            static_cast<unsigned>(f.capabilities.registerCapability("COMPANION")));
    f.display.fills.clear();
    f.display.texts.clear();
    shell.update({});
    const auto diamond = apps::homeCompanionPosition;
    TEST_ASSERT_EQUAL_INT(6, diamond.y);
    TEST_ASSERT_EQUAL_INT(94, diamond.x);
    TEST_ASSERT_TRUE(f.display.drewCompanionDiamond());
    TEST_ASSERT_FALSE(f.display.shows("COMPANION"));
    TEST_ASSERT_TRUE(f.display.texts.empty());
    for (const auto& fill : f.display.fills) {
        TEST_ASSERT_TRUE(fill.position.x >= diamond.x);
        TEST_ASSERT_TRUE(fill.position.x + fill.width <=
                         diamond.x + apps::homeCompanionIndicatorSize);
        TEST_ASSERT_TRUE(fill.position.y >= diamond.y);
        TEST_ASSERT_TRUE(fill.position.y + fill.height <=
                         diamond.y + apps::homeCompanionIndicatorSize);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(core::CapabilityRemovalResult::Removed),
                            static_cast<unsigned>(f.capabilities.removeCapability("COMPANION")));
    f.display.fills.clear();
    shell.update({});
    TEST_ASSERT_FALSE(f.display.drewCompanionDiamond());
    TEST_ASSERT_EQUAL_UINT(1, f.display.fills.size());
    TEST_ASSERT_EQUAL_INT(diamond.x, f.display.fills[0].position.x);
    TEST_ASSERT_EQUAL_INT(diamond.y, f.display.fills[0].position.y);
}

void test_six_settings_rows_step_values_and_repaint_only_changed_row() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({tab});
    for (const char* label : {"Bluetooth", "Wi-Fi", "Sound volume", "Screen timeout",
                              "Screen brightness", "LED brightness"})
        TEST_ASSERT_TRUE(f.display.shows(label));
    shell.update({down, down, down});
    TEST_ASSERT_TRUE(f.display.shows("Normal"));
    shell.update({right});
    TEST_ASSERT_TRUE(f.display.shows("Long"));
    TEST_ASSERT_TRUE(f.deviceSettings.screenTimeout() == core::ScreenTimeoutMode::Long);
    shell.update({down, left});
    TEST_ASSERT_EQUAL_UINT8(90, f.deviceSettings.screenBrightness());
    TEST_ASSERT_TRUE(f.display.shows("90%"));
    shell.update({down, right});
    TEST_ASSERT_EQUAL_UINT8(4, f.deviceSettings.ledBrightness());
    TEST_ASSERT_TRUE(f.display.shows("4%"));
    const auto frames = f.display.frames;
    shell.update({});
    TEST_ASSERT_EQUAL(frames, f.display.frames);
}

void test_led_brightness_key_wakes_without_changing_setting() {
    Fixture f;
    auto shell = f.makeShell();
    RuntimeBridge bridge(f);
    shell.update({tab, down, down, down, down, down});
    TEST_ASSERT_EQUAL_UINT8(3, f.deviceSettings.ledBrightness());
    bridge.idleOff();

    shell.update(bridge.press(right));
    TEST_ASSERT_TRUE(f.displayPower.state() == core::DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(3, f.deviceSettings.ledBrightness());
    (void)bridge.runtime.update(core::DisplayPowerController::wakeRampDuration);
    TEST_ASSERT_TRUE(f.displayPower.state() == core::DisplayPowerState::Awake);
    shell.update(bridge.press(right));
    TEST_ASSERT_EQUAL_UINT8(4, f.deviceSettings.ledBrightness());
}

void test_screen_brightness_key_wakes_without_changing_setting() {
    Fixture f;
    auto shell = f.makeShell();
    RuntimeBridge bridge(f);
    shell.update({tab, down, down, down, down});
    TEST_ASSERT_EQUAL_UINT8(100, f.deviceSettings.screenBrightness());
    bridge.idleOff();

    shell.update(bridge.press(left));
    TEST_ASSERT_TRUE(f.displayPower.state() == core::DisplayPowerState::Waking);
    TEST_ASSERT_EQUAL_UINT8(100, f.deviceSettings.screenBrightness());
    (void)bridge.runtime.update(core::DisplayPowerController::wakeRampDuration);
    TEST_ASSERT_TRUE(f.displayPower.state() == core::DisplayPowerState::Awake);
    shell.update(bridge.press(left));
    TEST_ASSERT_EQUAL_UINT8(90, f.deviceSettings.screenBrightness());
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_runtime_preserves_home_and_settings);
    RUN_TEST(test_home_companion_indicator_appears_only_when_companion_capability_is_live);
    RUN_TEST(test_home_orb_does_not_advance_while_display_off);
    RUN_TEST(test_home_entering_while_display_off_defers_ambient_frame);
    RUN_TEST(test_home_transition_freezes_ambient_phase_and_frame_interval);
    RUN_TEST(test_home_ambient_model_is_deterministic_and_bounded);
    RUN_TEST(test_home_bluetooth_status_uses_semantic_dot_colors);
    RUN_TEST(test_home_ambient_throttles_and_delayed_update_renders_once);
    RUN_TEST(test_home_reentry_starts_a_new_ambient_frame_interval);
    RUN_TEST(test_wake_consumed_frame_plays_no_cue_and_dispatches_no_action);
    RUN_TEST(test_home_enter_opens_launcher_and_tab_still_opens_settings);
    RUN_TEST(test_active_mini_app_receives_scheduled_update_instead_of_shell_input);
    RUN_TEST(test_explicit_runtime_deactivation_returns_shell_to_launcher);
    RUN_TEST(test_explicit_runtime_deactivation_restores_launcher_before_idle_update);
    RUN_TEST(test_capability_loss_returns_shell_to_launcher_without_forwarding_input);
    RUN_TEST(test_launcher_enter_activates_and_escape_returns_to_launcher);
    RUN_TEST(test_drawing_mini_app_handoff_fully_redraws_launcher_backward);
    RUN_TEST(test_capability_loss_from_launcher_uses_backward_transition_and_overlay);
    RUN_TEST(test_launcher_enter_then_down_does_not_move_hidden_selection);
    RUN_TEST(test_launcher_enter_then_escape_does_not_leave_launcher_route);
    RUN_TEST(test_mini_app_receives_printable_events_in_order);
    RUN_TEST(test_input_before_escape_reaches_mini_app);
    RUN_TEST(test_input_after_escape_is_routed_to_launcher);
    RUN_TEST(test_events_on_both_sides_of_escape_change_owner);
    RUN_TEST(test_app_initiated_close_routes_following_events_to_launcher);
    RUN_TEST(test_app_initiated_close_from_scheduled_tick_is_deferred);
    RUN_TEST(test_capability_loss_during_event_does_not_replay_into_launcher);
    RUN_TEST(test_mini_app_elapsed_advances_once_per_shell_update);
    RUN_TEST(test_mini_app_printable_event_plays_one_keypress);
    RUN_TEST(test_mini_app_escape_plays_one_keypress_and_restores_launcher);
    RUN_TEST(test_mini_app_batch_plays_one_cue_per_event);
    RUN_TEST(test_settings_volume_steps_play_directional_cues_without_keypress);
    RUN_TEST(test_launcher_navigation_plays_one_keypress_per_event);
    RUN_TEST(test_malformed_app_open_does_not_mutate_runtime);
    RUN_TEST(test_app_open_from_launcher_activates_when_idle);
    RUN_TEST(test_app_open_is_rejected_while_mini_app_is_active);
    RUN_TEST(test_six_settings_rows_step_values_and_repaint_only_changed_row);
    RUN_TEST(test_led_brightness_key_wakes_without_changing_setting);
    RUN_TEST(test_screen_brightness_key_wakes_without_changing_setting);
    return UNITY_END();
}
