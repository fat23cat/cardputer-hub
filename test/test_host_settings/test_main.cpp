#include "apps/hosts/host_settings.h"
#include "apps/network/wifi_settings.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/application_shell.h"
#include "apps/shell/home_graphics.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/text_layout.h"
#include "services/audio/audio_service.h"
#include "services/network/network_service.h"
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
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
    void beginFrame() override {
        TEST_ASSERT_FALSE(inFrame);
        inFrame = true;
        dirty = false;
    }
    void endFrame() override {
        TEST_ASSERT_TRUE(inFrame);
        inFrame = false;
        if (dirty)
            ++presentations;
    }
    void beginTransition(core::SlideDirection direction) override {
        transitions.push_back(direction);
    }
    std::vector<core::SlideDirection> transitions;
    bool inFrame = false, dirty = false;
    int presentations = 0;
    struct Rectangle {
        core::PixelPosition position;
        std::int32_t width;
        std::int32_t height;
    };
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        rectangles.push_back({position, width, height});
        std::ostringstream line;
        line << "R " << position.x << ' ' << position.y << ' ' << width << ' ' << height << ' '
             << unsigned(color.red) << ' ' << unsigned(color.green) << ' ' << unsigned(color.blue);
        commands.push_back(line.str());
        dirty = true;
    }
    void clear(core::RgbColor color) override {
        ++frames;
        dirty = true;
        texts.clear();
        drawnTexts.clear();
        commands = {"C " + std::to_string(color.red) + " " + std::to_string(color.green) + " " +
                    std::to_string(color.blue)};
    }
    void drawText(core::PixelPosition position, const char* value, core::TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        texts.push_back(value);
        drawnTexts.push_back({position, value});
        TEST_ASSERT_TRUE(position.x + std::string(value).size() * 6 * style.scale <= 240);
        TEST_ASSERT_TRUE(position.y + 8 * style.scale <= 135);
        std::ostringstream line;
        line << "T " << position.x << ' ' << position.y << ' ' << unsigned(style.scale) << ' '
             << unsigned(style.foreground.red) << ' ' << unsigned(style.foreground.green) << ' '
             << unsigned(style.foreground.blue) << ' ' << unsigned(style.background.red) << ' '
             << unsigned(style.background.green) << ' ' << unsigned(style.background.blue) << ' '
             << std::quoted(value);
        commands.push_back(line.str());
        dirty = true;
    }
    void capture(const char* name) const {
        if (const auto* directory = std::getenv("CARDPUTER_UI_CAPTURE_DIR")) {
            std::ofstream output(std::string(directory) + "/" + name + ".draw");
            TEST_ASSERT_TRUE(output.good());
            for (const auto& command : commands)
                output << command << '\n';
        }
    }
    std::optional<core::PixelPosition> textAt(const char* value, std::int32_t y) const {
        for (auto item = drawnTexts.rbegin(); item != drawnTexts.rend(); ++item) {
            if (item->value == value && item->position.y == y)
                return item->position;
        }
        return std::nullopt;
    }
    std::vector<std::string> commands;
    std::vector<Rectangle> rectangles;
    int frames = 0;
    std::vector<std::string> texts;
    struct DrawnText {
        core::PixelPosition position;
        std::string value;
    };
    std::vector<DrawnText> drawnTexts;
};
class Actions final : public core::IActionHandler {
  public:
    core::ActionHandlingResult handle(const core::Action& action) override {
        seen.push_back(action);
        return core::ActionHandlingResult::Handled;
    }
    std::vector<core::Action> seen;
};
class WifiAdapter final : public connectivity::IWifiAdapter {
  public:
    connectivity::WifiAdapterResult initializeStation() override { return {}; }
    connectivity::WifiAdapterResult connect(const connectivity::WifiNetworkConfig&) override {
        return {};
    }
    connectivity::WifiAdapterResult disconnect() override { return {}; }
    connectivity::WifiAdapterState state() const override { return adapterState; }
    std::optional<std::int32_t> signalStrengthDbm() const override { return rssi; }
    connectivity::WifiAdapterState adapterState = connectivity::WifiAdapterState::Disconnected;
    std::optional<std::int32_t> rssi;
};
class AudioAdapter final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t volumePercent) override {
        volumes.push_back(volumePercent);
        return true;
    }
    void setVolume(std::uint8_t volumePercent) override { volumes.push_back(volumePercent); }
    bool isPlaying() const override { return false; }
    bool play(const core::AudioClip& clip) override {
        clips.push_back(clip);
        return true;
    }
    std::vector<std::uint8_t> volumes;
    std::vector<core::AudioClip> clips;
};
// Use the production Service state view without starting its hardware backend.
// This adapter must remain unused by the UI: intentions go through ActionBus.
class Adapter final : public connectivity::IBluetoothAdapter {
  public:
    connectivity::BluetoothAdapterResult initialize(const connectivity::BluetoothDeviceConfig&,
                                                    std::uint32_t lifecycle) override {
        if (!hardwareExpected)
            TEST_FAIL_MESSAGE("UI touched hardware");
        activeLifecycle = lifecycle;
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
    connectivity::BluetoothPollResult pollEvent() override {
        if (events.empty())
            return {};
        const auto event = events.front();
        events.pop_front();
        return connectivity::BluetoothPollResult::withEvent(event);
    }
    connectivity::BluetoothBondQueryResult bondState(connectivity::BluetoothPeerHandle) override {
        return hardwareExpected ? connectivity::BluetoothBondQueryResult::Unbonded
                                : connectivity::BluetoothBondQueryResult{};
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
    connectivity::BluetoothBondListResult bonds() override {
        return hardwareExpected
                   ? connectivity::
                         BluetoothBondListResult{connectivity::BluetoothBondListStatus::Success,
                                                 bonded}
                   : connectivity::BluetoothBondListResult{};
    }
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
        TEST_FAIL_MESSAGE("UI sent host input");
        return {};
    }
    connectivity::BluetoothHidAdapterResult
    releaseHidReports(connectivity::BluetoothPeerHandle) override {
        return {};
    }
    bool hardwareExpected = false;
    std::uint32_t activeLifecycle = 0;
    std::deque<connectivity::BluetoothEvent> events;
    std::vector<connectivity::BluetoothBondReference> bonded;
};
struct Fixture {
    Memory memory;
    core::Storage storage{memory};
    services::ConfigurationService config{storage};
    AudioAdapter audioAdapter;
    services::AudioService audio{config, audioAdapter};
    Adapter adapter;
    connectivity::BluetoothService bluetooth{adapter};
    services::HostService hosts{bluetooth, config};
    WifiAdapter wifiAdapter;
    connectivity::WiFiService wifi{wifiAdapter};
    services::NetworkService network{wifi, config};
    core::ActionBus bus;
    Actions actions;
    Display display;
    apps::HostSettings ui{hosts, bus, display};
    apps::WiFiSettings wifiSettings{network, bus, display};
    core::AppRegistry appRegistry;
    core::CapabilityRegistry capabilities;
    apps::MiniAppRuntime miniApps{appRegistry, capabilities};
    Fixture() {
        services::SystemConfiguration value;
        value.host.hosts = {{8, "Office laptop", {}, std::nullopt, {}, std::nullopt},
                            {9, "Travel laptop", {}, std::nullopt, {}, std::nullopt}};
        value.host.hosts[0].bond.bytes[0] = 1;
        value.host.hosts[1].bond.bytes[0] = 2;
        value.host.nextHostId = 10;
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(audio.start() == services::AudioResult::Success);
        audioAdapter.clips.clear();
        TEST_ASSERT_TRUE(bus.registerHandler("audio.volume.step", audio) ==
                         core::RegistrationResult::Registered);
        for (const auto* id : {"network.set-enabled", "network.configure", "network.forget"})
            TEST_ASSERT_TRUE(bus.registerHandler(id, network) ==
                             core::RegistrationResult::Registered);
        for (const auto* id : {"host.select", "host.rename", "host.bluetooth"})
            bus.registerHandler(id, actions);
    }
};
core::InputEvent character(char c) { return {core::InputEventType::PrintableCharacter, c, {}, {}}; }
const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};

void test_navigation_uses_unmodified_arrow_keys_but_rename_keeps_punctuation() {
    Fixture f;
    f.ui.update({character('.'), character('.'), character('.'), character(';'), enter, enter});
    TEST_ASSERT_EQUAL_UINT(1, f.actions.seen.size());
    TEST_ASSERT_EQUAL_STRING("host.select", f.actions.seen.back().id.c_str());
    TEST_ASSERT_EQUAL_INT(8, std::get<std::int32_t>(*f.actions.seen.back().findParameter("id")));
    f.ui.update({character('r'), character(';'), character('.'), enter});
    TEST_ASSERT_EQUAL_UINT(2, f.actions.seen.size());
    TEST_ASSERT_EQUAL_STRING("host.rename", f.actions.seen.back().id.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Office laptop;.",
        std::get<std::string>(*f.actions.seen.back().findParameter("name")).c_str());
}

void test_bluetooth_footer_is_quiet_and_x_has_no_destructive_action() {
    Fixture f;
    f.ui.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Esc Home") ==
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") ==
                     f.display.texts.end());
    for (const auto& text : f.display.texts) {
        TEST_ASSERT_TRUE(text.find("Move") == std::string::npos);
        TEST_ASSERT_TRUE(text.find("Enter") == std::string::npos);
        TEST_ASSERT_TRUE(text.find("Forget all") == std::string::npos);
    }
    f.ui.update({character('x')});
    TEST_ASSERT_FALSE(f.ui.modal());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_host_menu_back_and_incremental_navigation() {
    Fixture f;
    f.ui.update({down, down, enter});
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Esc Back") ==
                     f.display.texts.end());
    TEST_ASSERT_TRUE(
        std::none_of(f.display.rectangles.begin(), f.display.rectangles.end(),
                     [](const auto& rectangle) { return rectangle.position.y == 118; }));
    f.display.capture("host-actions");
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({down});
    TEST_ASSERT_EQUAL_UINT(2, f.display.rectangles.size());
    f.ui.update({down, enter});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "DELETE HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER DELETE") !=
                     f.display.texts.end());
    f.display.capture("host-delete");
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    f.ui.update({escape});
    TEST_ASSERT_TRUE(f.ui.modal());
    f.ui.update({escape});
    TEST_ASSERT_FALSE(f.ui.modal());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_transactional_modals_show_contextual_footers() {
    Fixture host;
    host.ui.update({down, down, enter, down, enter});
    TEST_ASSERT_TRUE(std::find(host.display.texts.begin(), host.display.texts.end(), "HOST NAME") !=
                     host.display.texts.end());
    TEST_ASSERT_TRUE(std::find(host.display.texts.begin(), host.display.texts.end(),
                               "ESC CANCEL") != host.display.texts.end());
    TEST_ASSERT_TRUE(std::find(host.display.texts.begin(), host.display.texts.end(),
                               "ENTER APPLY") != host.display.texts.end());

    Fixture pairing;
    pairing.adapter.hardwareExpected = true;
    TEST_ASSERT_TRUE(pairing.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(pairing.bus.registerHandler("host.pair", pairing.hosts) ==
                     core::RegistrationResult::Registered);
    pairing.ui.update({down, enter});
    TEST_ASSERT_TRUE(std::find(pairing.display.texts.begin(), pairing.display.texts.end(),
                               "ADD DEVICE") != pairing.display.texts.end());
    TEST_ASSERT_TRUE(std::find(pairing.display.texts.begin(), pairing.display.texts.end(),
                               "ESC CANCEL") != pairing.display.texts.end());
    TEST_ASSERT_TRUE(std::find(pairing.display.texts.begin(), pairing.display.texts.end(),
                               "ENTER YES") == pairing.display.texts.end());
    TEST_ASSERT_TRUE(std::find(pairing.display.texts.begin(), pairing.display.texts.end(),
                               "ENTER APPLY") == pairing.display.texts.end());
}

void test_rename_input_redraws_once_and_unchanged_rename_state_stays_idle() {
    Fixture f;
    f.ui.update({down, down, enter, down, enter});
    const auto frames = f.display.frames;

    f.ui.update({character('X')});

    TEST_ASSERT_EQUAL(frames + 1, f.display.frames);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Office laptopX") !=
                     f.display.texts.end());
    f.ui.update({});
    TEST_ASSERT_EQUAL(frames + 1, f.display.frames);
}

void test_host_status_changes_redraw_only_status_and_bluetooth_row() {
    Fixture f;
    auto value = f.config.value();
    value.host.activeHost = 8;
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.ui.update({});
    f.display.rectangles.clear();
    f.display.texts.clear();
    f.adapter.hardwareExpected = true;
    f.adapter.bonded.push_back(value.host.hosts.front().bond);
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.setEnabled(true) == services::HostResult::Success);

    f.ui.update({});

    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "CONNECTING") !=
                     f.display.texts.end());
    TEST_ASSERT_EQUAL_UINT(2, f.display.rectangles.size());
    TEST_ASSERT_EQUAL_INT32(6, f.display.rectangles.front().position.y);
    TEST_ASSERT_EQUAL_INT32(24, f.display.rectangles.back().position.y);
}

void test_bluetooth_header_status_is_right_aligned() {
    Fixture f;
    f.ui.update({});
    const auto off = f.display.textAt("OFF", 6);
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_EQUAL_INT32(core::rightAlignedTextX("OFF"), off->x);

    auto value = f.config.value();
    value.host.activeHost = 8;
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.adapter.hardwareExpected = true;
    f.adapter.bonded.push_back(value.host.hosts.front().bond);
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.setEnabled(true) == services::HostResult::Success);
    f.ui.update({});
    const auto connecting = f.display.textAt("CONNECTING", 6);
    TEST_ASSERT_TRUE(connecting.has_value());
    TEST_ASSERT_EQUAL_INT32(core::rightAlignedTextX("CONNECTING"), connecting->x);
}

void test_home_bluetooth_status_change_redraws_only_the_host_section() {
    Fixture f;
    auto value = f.config.value();
    value.host.activeHost = 8;
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.adapter.hardwareExpected = true;
    f.adapter.bonded.push_back(value.host.hosts.front().bond);
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    f.display.rectangles.clear();
    f.display.texts.clear();
    const auto presentations = f.display.presentations;

    TEST_ASSERT_TRUE(f.hosts.setEnabled(true) == services::HostResult::Success);
    shell.update({});

    TEST_ASSERT_EQUAL(presentations + 1, f.display.presentations);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "CONNECTING") !=
                     f.display.texts.end());
    for (const auto& rectangle : f.display.rectangles) {
        TEST_ASSERT_TRUE(rectangle.position.y >= 32 &&
                         rectangle.position.y + rectangle.height <= 97);
    }
    shell.update({});
    TEST_ASSERT_EQUAL(presentations + 1, f.display.presentations);
}

void test_home_and_bluetooth_settings_show_the_same_pairing_status() {
    Fixture f;
    f.adapter.hardwareExpected = true;
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.hosts.startPairing() == services::HostResult::Success);
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);

    shell.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "PAIRING") !=
                     f.display.texts.end());

    const core::InputEvent tab{core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {}};
    shell.update({tab});
    shell.update({enter});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ADD DEVICE") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "PAIRING") !=
                     f.display.texts.end());
}

void test_pairing_prompt_change_redraws_pairing_content_then_stays_idle() {
    Fixture f;
    f.adapter.hardwareExpected = true;
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.bus.registerHandler("host.pair", f.hosts) ==
                     core::RegistrationResult::Registered);
    f.ui.update({down, enter});
    const auto waitingFrames = f.display.frames;
    f.adapter.events.push_back({connectivity::BluetoothEventType::AdvertisingStarted,
                                {},
                                connectivity::BluetoothFailureClass::Fatal,
                                f.adapter.activeLifecycle});
    f.adapter.events.push_back({connectivity::BluetoothEventType::PeerConnected,
                                {8},
                                connectivity::BluetoothFailureClass::Fatal,
                                f.adapter.activeLifecycle});
    connectivity::BluetoothEvent challenge{connectivity::BluetoothEventType::PairingChallenge,
                                           {8},
                                           connectivity::BluetoothFailureClass::Fatal,
                                           f.adapter.activeLifecycle};
    challenge.challengeType = connectivity::BluetoothPairingChallengeType::ConfirmComparison;
    challenge.challengeValue = 654321;
    f.adapter.events.push_back(challenge);
    f.hosts.update(std::chrono::milliseconds(0));

    f.ui.update({});

    TEST_ASSERT_EQUAL(waitingFrames + 1, f.display.frames);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(),
                               "Does the computer show this code?") != f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER YES") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER APPLY") ==
                     f.display.texts.end());
    f.ui.update({});
    TEST_ASSERT_EQUAL(waitingFrames + 1, f.display.frames);
}

void test_pairing_passkey_entry_shows_enter_apply_when_complete() {
    Fixture f;
    f.adapter.hardwareExpected = true;
    TEST_ASSERT_TRUE(f.hosts.start() == services::HostResult::Success);
    TEST_ASSERT_TRUE(f.bus.registerHandler("host.pair", f.hosts) ==
                     core::RegistrationResult::Registered);
    f.ui.update({down, enter});
    f.adapter.events.push_back({connectivity::BluetoothEventType::AdvertisingStarted,
                                {},
                                connectivity::BluetoothFailureClass::Fatal,
                                f.adapter.activeLifecycle});
    f.adapter.events.push_back({connectivity::BluetoothEventType::PeerConnected,
                                {8},
                                connectivity::BluetoothFailureClass::Fatal,
                                f.adapter.activeLifecycle});
    connectivity::BluetoothEvent challenge{connectivity::BluetoothEventType::PairingChallenge,
                                           {8},
                                           connectivity::BluetoothFailureClass::Fatal,
                                           f.adapter.activeLifecycle};
    challenge.challengeType = connectivity::BluetoothPairingChallengeType::EnterPasskey;
    f.adapter.events.push_back(challenge);
    f.hosts.update(std::chrono::milliseconds(0));
    f.ui.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(),
                               "Type the code from the computer") != f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER APPLY") ==
                     f.display.texts.end());
    f.ui.update({character('1'), character('2'), character('3'), character('4'), character('5')});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER APPLY") ==
                     f.display.texts.end());
    f.ui.update({character('6')});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(),
                               "Type the code from the computer") != f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER APPLY") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER YES") ==
                     f.display.texts.end());
}

void test_focus_move_only_repaints_changed_rows_without_clearing_screen() {
    Fixture f;
    f.ui.update({});
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({down});
    TEST_ASSERT_EQUAL(1, f.display.frames);
    TEST_ASSERT_EQUAL_UINT(5, f.display.texts.size());
    TEST_ASSERT_EQUAL_UINT(2, f.display.rectangles.size());
    for (const auto& rectangle : f.display.rectangles) {
        TEST_ASSERT_TRUE(rectangle.position.y >= 24 &&
                         rectangle.position.y + rectangle.height <= 94);
    }
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({});
    TEST_ASSERT_TRUE(f.display.texts.empty());
    TEST_ASSERT_TRUE(f.display.rectangles.empty());
}

void test_scrolling_repaints_list_content_and_return_from_rename_invalidates_list_cache() {
    Fixture f;
    auto value = f.config.value();
    value.host.hosts.push_back({10, "Third laptop", {}, std::nullopt, {}, std::nullopt});
    value.host.hosts.back().bond.bytes[0] = 3;
    value.host.nextHostId = 11;
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.ui.update({down, down, down});
    f.display.texts.clear();
    f.display.rectangles.clear();
    f.ui.update({down});
    TEST_ASSERT_EQUAL(1, f.display.frames);
    TEST_ASSERT_EQUAL_UINT(8, f.display.texts.size());
    TEST_ASSERT_EQUAL_STRING("02", f.display.texts.front().c_str());
    TEST_ASSERT_EQUAL_STRING("Add device", f.display.texts[1].c_str());
    TEST_ASSERT_EQUAL_STRING("Third laptop", f.display.texts.back().c_str());
    for (const auto& rectangle : f.display.rectangles) {
        TEST_ASSERT_TRUE(rectangle.position.y >= 24 &&
                         rectangle.position.y + rectangle.height <= 94);
    }
    f.ui.update({character('r')});
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    f.ui.update({escape});
    TEST_ASSERT_EQUAL(3, f.display.frames);
    TEST_ASSERT_EQUAL_STRING("BLUETOOTH", f.display.texts.front().c_str());
    f.display.texts.clear();
    f.ui.update({});
    TEST_ASSERT_TRUE(f.display.texts.empty());
}

const core::InputEvent settingsChord{core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {}};
const core::InputEvent fnTab{
    core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {false, false, false, false, true}};
void test_shell_boots_simple_home_and_opens_settings_before_bluetooth() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "OFF") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(),
                               "BLUETOOTH SETTINGS") == f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") ==
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER SELECT") ==
                     f.display.texts.end());
    f.display.capture("home");
    const auto frames = f.display.frames;
    shell.update({enter, character('b')});
    TEST_ASSERT_EQUAL(frames, f.display.frames);
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    shell.update({settingsChord});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Bluetooth") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") ==
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER SELECT") ==
                     f.display.texts.end());
    f.display.capture("settings");
    const auto settingsFrames = f.display.frames;
    shell.update({settingsChord});
    TEST_ASSERT_EQUAL(settingsFrames, f.display.frames);
    shell.update({enter});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "BLUETOOTH") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ESC CANCEL") ==
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "ENTER YES") ==
                     f.display.texts.end());
    f.display.capture("bluetooth");
    shell.update({settingsChord}); // From the BLE list, return to Settings.
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    shell.update({character('`')});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    shell.update({settingsChord, enter, character('`')}); // Preserve Esc Home in BLE.
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_shell_back_cancels_rename_before_returning_home_and_preserves_host_intent() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    shell.update({settingsChord, enter, character('.'), character('.'), character('r')});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "HOST NAME") !=
                     f.display.texts.end());
    f.display.capture("rename");
    const auto editingFrames = f.display.frames;
    shell.update({settingsChord});
    TEST_ASSERT_EQUAL(editingFrames, f.display.frames);
    shell.update({escape});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "BLUETOOTH") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
    shell.update({escape});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_home_and_panel_show_selected_host_and_do_not_navigate_on_state_updates() {
    Fixture f;
    auto value = f.config.value();
    value.host.activeHost = 9;
    value.host.hosts.back().name = "Work laptop for projects";
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    f.display.capture("home-selected");
    shell.update({settingsChord, enter});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED") !=
                     f.display.texts.end());
    f.display.capture("bluetooth-selected");
    value.host.hosts.back().name = "Renamed host";
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    f.display.texts.clear();
    shell.update({});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "HOME") ==
                     f.display.texts.end());
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "Renamed host") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_plain_tab_opens_settings_and_leaves_editing_intact() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    const core::InputEvent tab{core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {}};
    shell.update({tab});
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SETTINGS") !=
                     f.display.texts.end());
    shell.update({enter, down, down, character('r')});
    const auto presentations = f.display.presentations;
    shell.update({tab});
    TEST_ASSERT_TRUE(f.ui.modal());
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_page_transitions_follow_navigation_and_ignore_focus_or_status_refresh() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    TEST_ASSERT_TRUE(f.display.transitions.empty());
    shell.update({settingsChord});
    TEST_ASSERT_EQUAL_UINT(1, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Forward);
    shell.update({enter});
    TEST_ASSERT_EQUAL_UINT(2, f.display.transitions.size());
    shell.update({down, down});
    TEST_ASSERT_EQUAL_UINT(2, f.display.transitions.size());
    shell.update({enter});
    TEST_ASSERT_EQUAL_UINT(3, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Forward);
    shell.update({character('r')});
    TEST_ASSERT_EQUAL_UINT(4, f.display.transitions.size());
    const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
    shell.update({escape});
    TEST_ASSERT_EQUAL_UINT(5, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
    shell.update({escape, settingsChord, escape});
    TEST_ASSERT_EQUAL_UINT(8, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
    const auto transitions = f.display.transitions.size();
    shell.update({}, std::chrono::milliseconds(500));
    TEST_ASSERT_EQUAL_UINT(transitions, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_fn_tab_and_system_button_do_not_open_settings() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    const core::InputEvent menu{core::InputEventType::NamedKey, 0, core::NamedKey::SystemMenu, {}};
    shell.update({});
    const auto presentations = f.display.presentations;
    shell.update({fnTab, menu});
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "SELECTED HOST") !=
                     f.display.texts.end());
    TEST_ASSERT_TRUE(f.display.transitions.empty());
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_home_name_fits_without_changing_the_saved_label() {
    Fixture f;
    auto value = f.config.value();
    value.host.activeHost = 9;
    value.host.hosts.back().name = std::string(24, 'W');
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    const auto label = apps::fitHomeHostName(value.host.hosts.back().name);
    TEST_ASSERT_TRUE(label.size() < 24);
    TEST_ASSERT_EQUAL_STRING("...", label.substr(label.size() - 3).c_str());
    TEST_ASSERT_EQUAL_STRING("MACBOOK PRO", apps::fitHomeHostName("MacBook Pro").c_str());
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    f.display.capture("home-long-name");
    const auto oldCommands = f.display.commands;
    value.host.hosts.back().name = "MacBook Pro";
    TEST_ASSERT_TRUE(f.config.save(value) == services::ConfigurationResult::Success);
    shell.update({});
    TEST_ASSERT_TRUE(oldCommands != f.display.commands);
    TEST_ASSERT_EQUAL_STRING("MacBook Pro", f.config.value().host.hosts.back().name.c_str());
    f.display.capture("home-compact-name");
}

void test_home_wave_is_bounded_and_pauses_in_settings() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    const auto frames = f.display.frames;
    const auto presentations = f.display.presentations;
    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({}, std::chrono::milliseconds(499));
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    shell.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_EQUAL(presentations + 1, f.display.presentations);
    TEST_ASSERT_EQUAL(frames, f.display.frames);
    TEST_ASSERT_TRUE(f.display.texts.empty());
    TEST_ASSERT_FALSE(f.display.rectangles.empty());
    for (const auto& r : f.display.rectangles)
        TEST_ASSERT_TRUE(r.position.y >= 99 && r.position.y + r.height <= 135);
    f.display.capture("home-wave");
    shell.update({settingsChord});
    const auto settingsPresentations = f.display.presentations;
    shell.update({}, std::chrono::milliseconds(32000));
    TEST_ASSERT_EQUAL(settingsPresentations, f.display.presentations);
    shell.update({enter});
    const auto blePresentations = f.display.presentations;
    shell.update({}, std::chrono::milliseconds(32000));
    TEST_ASSERT_EQUAL(blePresentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.actions.seen.empty());
}

void test_every_semantic_key_press_gets_one_click_and_idle_updates_stay_silent() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    TEST_ASSERT_TRUE(f.audioAdapter.clips.empty());
    shell.update({character('a'), enter});
    TEST_ASSERT_EQUAL_UINT(2, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT(1280, f.audioAdapter.clips[0].sampleCount);
    shell.update({});
    TEST_ASSERT_EQUAL_UINT(2, f.audioAdapter.clips.size());
}

void test_settings_volume_row_steps_with_left_and_right_and_zero_is_mute() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    const core::InputEvent left{core::InputEventType::NamedKey, 0, core::NamedKey::Left, {}};
    const core::InputEvent right{core::InputEventType::NamedKey, 0, core::NamedKey::Right, {}};
    shell.update({settingsChord});
    for (const auto* label : {"01", "Bluetooth", "02", "Wi-Fi", "03", "Sound volume", "60%"})
        TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), label) !=
                         f.display.texts.end());

    f.audioAdapter.clips.clear();
    shell.update({down, down, right});
    TEST_ASSERT_EQUAL_UINT8(70, f.config.value().soundVolume);
    TEST_ASSERT_EQUAL_UINT8(70, f.audioAdapter.volumes.back());
    TEST_ASSERT_EQUAL_UINT(3, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT(1760, f.audioAdapter.clips.back().sampleCount);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "70%") !=
                     f.display.texts.end());

    for (int index = 0; index < 7; ++index)
        shell.update({left});
    TEST_ASSERT_EQUAL_UINT8(0, f.config.value().soundVolume);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "0%") !=
                     f.display.texts.end());
    const auto mutedPlayCount = f.audioAdapter.clips.size();
    shell.update({character('x')});
    TEST_ASSERT_EQUAL_UINT(mutedPlayCount, f.audioAdapter.clips.size());

    shell.update({right});
    TEST_ASSERT_EQUAL_UINT8(10, f.config.value().soundVolume);
    TEST_ASSERT_EQUAL_UINT8(10, f.audioAdapter.volumes.back());
    TEST_ASSERT_EQUAL_UINT(mutedPlayCount + 1, f.audioAdapter.clips.size());
    TEST_ASSERT_EQUAL_UINT(1760, f.audioAdapter.clips.back().sampleCount);
}

void test_settings_volume_accepts_the_cardputer_arrow_marked_keys_without_fn() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({settingsChord});
    shell.update({character('.'), character('.')});

    shell.update({character('/')});
    TEST_ASSERT_EQUAL_UINT8(70, f.config.value().soundVolume);

    shell.update({character(',')});
    TEST_ASSERT_EQUAL_UINT8(60, f.config.value().soundVolume);
}

void test_settings_focus_and_volume_only_repaint_changed_rows() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    const core::InputEvent right{core::InputEventType::NamedKey, 0, core::NamedKey::Right, {}};
    shell.update({settingsChord});
    const auto fullFrames = f.display.frames;

    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({down});
    TEST_ASSERT_EQUAL(fullFrames, f.display.frames);
    TEST_ASSERT_EQUAL_UINT(2, f.display.rectangles.size());
    TEST_ASSERT_EQUAL_UINT(4, f.display.texts.size());
    for (const auto& rectangle : f.display.rectangles) {
        TEST_ASSERT_TRUE(rectangle.position.y >= 24 &&
                         rectangle.position.y + rectangle.height <= 58);
    }

    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({down});
    TEST_ASSERT_EQUAL(fullFrames, f.display.frames);
    TEST_ASSERT_EQUAL_UINT(2, f.display.rectangles.size());
    for (const auto& rectangle : f.display.rectangles) {
        TEST_ASSERT_TRUE(rectangle.position.y >= 42 &&
                         rectangle.position.y + rectangle.height <= 76);
    }

    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({right});
    TEST_ASSERT_EQUAL(fullFrames, f.display.frames);
    TEST_ASSERT_EQUAL_UINT(1, f.display.rectangles.size());
    TEST_ASSERT_EQUAL_UINT(3, f.display.texts.size());
    TEST_ASSERT_EQUAL_INT32(60, f.display.rectangles.front().position.y);
}

void test_home_shows_unavailable_telemetry_and_updates_only_battery_region() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.ui, f.wifiSettings,
                                 f.audio, f.miniApps);
    shell.update({});
    for (const auto* label : {"--:--", "--%"})
        TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), label) !=
                         f.display.texts.end());
    for (const auto* forbidden : {"OFFLINE", "ONLINE", "CONNECTING", "ERROR"})
        TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), forbidden) ==
                         f.display.texts.end());
    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({}, std::chrono::milliseconds(0), 100);
    TEST_ASSERT_TRUE(std::find(f.display.texts.begin(), f.display.texts.end(), "100%") !=
                     f.display.texts.end());
    for (const auto& r : f.display.rectangles)
        TEST_ASSERT_TRUE(r.position.y + r.height <= 24);
    // The battery region contains only the clearing rectangle and percentage text.
    TEST_ASSERT_EQUAL_UINT(1, f.display.rectangles.size());
    f.display.capture("home-battery");
    const auto presentations = f.display.presentations;
    shell.update({}, std::chrono::milliseconds(0), 100);
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
}

void test_settings_dispatch_selection_and_do_not_redraw_an_unchanged_screen() {
    Memory memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    services::SystemConfiguration value;
    value.host.hosts = {{8, "Office laptop", {}, std::nullopt, {}, std::nullopt},
                        {9, "Travel laptop", {}, std::nullopt, {}, std::nullopt}};
    value.host.hosts[0].bond.bytes[0] = 1;
    value.host.hosts[1].bond.bytes[0] = 2;
    value.host.nextHostId = 10;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
    Adapter adapter;
    connectivity::BluetoothService bluetooth(adapter);
    services::HostService hosts(bluetooth, config);
    core::ActionBus bus;
    Actions actions;
    bus.registerHandler("host.select", actions);
    Display display;
    apps::HostSettings ui(hosts, bus, display);
    ui.update({});
    TEST_ASSERT_EQUAL(1, display.frames);
    ui.update({});
    TEST_ASSERT_EQUAL(1, display.frames);
    core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};
    core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
    ui.update({down, down, down, enter, enter});
    TEST_ASSERT_EQUAL_UINT(1, actions.seen.size());
    TEST_ASSERT_EQUAL_STRING("host.select", actions.seen[0].id.c_str());
    TEST_ASSERT_EQUAL_INT(9, std::get<std::int32_t>(*actions.seen[0].findParameter("id")));
}
} // namespace
void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_plain_tab_opens_settings_and_leaves_editing_intact);
    RUN_TEST(test_page_transitions_follow_navigation_and_ignore_focus_or_status_refresh);
    RUN_TEST(test_fn_tab_and_system_button_do_not_open_settings);
    RUN_TEST(test_home_name_fits_without_changing_the_saved_label);
    RUN_TEST(test_home_wave_is_bounded_and_pauses_in_settings);
    RUN_TEST(test_home_shows_unavailable_telemetry_and_updates_only_battery_region);
    RUN_TEST(test_host_menu_back_and_incremental_navigation);
    RUN_TEST(test_transactional_modals_show_contextual_footers);
    RUN_TEST(test_rename_input_redraws_once_and_unchanged_rename_state_stays_idle);
    RUN_TEST(test_host_status_changes_redraw_only_status_and_bluetooth_row);
    RUN_TEST(test_bluetooth_header_status_is_right_aligned);
    RUN_TEST(test_home_bluetooth_status_change_redraws_only_the_host_section);
    RUN_TEST(test_home_and_bluetooth_settings_show_the_same_pairing_status);
    RUN_TEST(test_pairing_prompt_change_redraws_pairing_content_then_stays_idle);
    RUN_TEST(test_pairing_passkey_entry_shows_enter_apply_when_complete);
    RUN_TEST(test_bluetooth_footer_is_quiet_and_x_has_no_destructive_action);
    RUN_TEST(test_home_and_panel_show_selected_host_and_do_not_navigate_on_state_updates);
    RUN_TEST(test_shell_boots_simple_home_and_opens_settings_before_bluetooth);
    RUN_TEST(test_shell_back_cancels_rename_before_returning_home_and_preserves_host_intent);
    RUN_TEST(test_scrolling_repaints_list_content_and_return_from_rename_invalidates_list_cache);
    RUN_TEST(test_navigation_uses_unmodified_arrow_keys_but_rename_keeps_punctuation);
    RUN_TEST(test_focus_move_only_repaints_changed_rows_without_clearing_screen);
    RUN_TEST(test_settings_dispatch_selection_and_do_not_redraw_an_unchanged_screen);
    RUN_TEST(test_every_semantic_key_press_gets_one_click_and_idle_updates_stay_silent);
    RUN_TEST(test_settings_volume_row_steps_with_left_and_right_and_zero_is_mute);
    RUN_TEST(test_settings_volume_accepts_the_cardputer_arrow_marked_keys_without_fn);
    RUN_TEST(test_settings_focus_and_volume_only_repaint_changed_rows);
    return UNITY_END();
}
