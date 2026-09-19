#include "apps/hosts/host_settings.h"
#include "apps/network/wifi_settings.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/application_shell.h"
#include "apps/shell/home_graphics.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "services/audio/audio_service.h"
#include "services/network/network_service.h"

#include <algorithm>
#include <chrono>
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

class Display final : public core::IDisplayAdapter {
  public:
    void beginFrame() override { dirty = false; }
    void endFrame() override {
        if (dirty)
            ++presentations;
    }
    void beginTransition(core::SlideDirection direction) override {
        transitions.push_back(direction);
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
        const auto origin = apps::homeCompanionIndicatorPosition("No host selected");
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
        TEST_ASSERT_TRUE(bus.registerHandler("audio.volume.step", audio) ==
                         core::RegistrationResult::Registered);
        for (const auto* id : {"network.set-enabled", "network.configure", "network.forget"})
            TEST_ASSERT_TRUE(bus.registerHandler(id, network) ==
                             core::RegistrationResult::Registered);
    }
    apps::ApplicationShell makeShell() {
        return apps::ApplicationShell(hosts, network, bus, display, hostSettings, wifiSettings,
                                      audio, miniApps, capabilities);
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
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
    TEST_ASSERT_TRUE(f.display.shows("SELECTED HOST"));
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
    TEST_ASSERT_TRUE(f.display.shows("SELECTED HOST"));
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
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
    f.registerApp("weather");
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned int>(f.miniApps.activate("weather")));
    shell.update({}, std::chrono::milliseconds(16));
    TEST_ASSERT_TRUE(f.display.shows("MINI APP"));
    TEST_ASSERT_FALSE(f.display.shows("--:--"));

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

void test_home_wave_does_not_advance_while_display_off() {
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
    TEST_ASSERT_TRUE(countWaveFills(f.display) > 0);
}

void test_wake_consumed_frame_plays_no_cue_and_dispatches_no_action() {
    Fixture f;
    auto shell = f.makeShell();
    shell.update({});
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
    f.audioAdapter.clips.clear();
    f.display.transitions.clear();

    // SystemRuntime already consumed the wake press, so this frame has no event.
    shell.update({}, std::chrono::milliseconds(16), std::nullopt, true);

    TEST_ASSERT_EQUAL_UINT(0, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT(0, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
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
    shell.update({});
    const auto diamond = apps::homeCompanionIndicatorPosition("No host selected");
    TEST_ASSERT_EQUAL_INT(apps::homeCompanionIndicatorY, diamond.y);
    TEST_ASSERT_TRUE(diamond.x > apps::homeHostNameOriginX);
    TEST_ASSERT_TRUE(f.display.drewCompanionDiamond());
    TEST_ASSERT_FALSE(f.display.shows("COMPANION"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(core::CapabilityRemovalResult::Removed),
                            static_cast<unsigned>(f.capabilities.removeCapability("COMPANION")));
    f.display.fills.clear();
    shell.update({});
    TEST_ASSERT_FALSE(f.display.drewCompanionDiamond());
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_runtime_preserves_home_and_settings);
    RUN_TEST(test_home_companion_indicator_appears_only_when_companion_capability_is_live);
    RUN_TEST(test_home_wave_does_not_advance_while_display_off);
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
    return UNITY_END();
}
