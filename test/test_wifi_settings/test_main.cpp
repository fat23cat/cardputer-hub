#include "apps/hosts/host_settings.h"
#include "apps/network/wifi_settings.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/application_shell.h"
#include "apps/shell/home_graphics.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"
#include "services/audio/audio_service.h"
#include "services/network/network_service.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <unity.h>
#include <vector>

using namespace cardputer_hub;

namespace {
class Memory final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {bytes.empty() ? core::StorageReadStatus::NotFound : core::StorageReadStatus::Found,
                bytes};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes& value) override {
        if (writeError)
            return core::StorageWriteStatus::BackendError;
        bytes = value;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
    core::StorageBytes bytes;
    bool writeError = false;
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
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor color) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        rectangles.push_back({position, width, height, color});
        dirty = true;
    }
    void clear(core::RgbColor) override {
        ++frames;
        texts.clear();
        drawnTexts.clear();
        rectangles.clear();
        dirty = true;
    }
    void drawText(core::PixelPosition position, const char* value, core::TextStyle style) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.x < 240 && position.y >= 0 &&
                         position.y < 135);
        TEST_ASSERT_TRUE(position.x + std::string(value).size() * 6 * style.scale <= 240);
        TEST_ASSERT_TRUE(position.y + 8 * style.scale <= 135);
        texts.push_back(value);
        drawnTexts.push_back({position, value});
        dirty = true;
    }
    bool shows(const char* value) const {
        return std::find(texts.begin(), texts.end(), value) != texts.end();
    }
    bool hasColor(core::RgbColor color) const {
        return std::any_of(rectangles.begin(), rectangles.end(), [&](const Rectangle& rectangle) {
            return rectangle.color.red == color.red && rectangle.color.green == color.green &&
                   rectangle.color.blue == color.blue;
        });
    }
    bool regionHasColor(core::PixelPosition origin, std::int32_t width, std::int32_t height,
                        core::RgbColor color) const {
        return std::any_of(rectangles.begin(), rectangles.end(), [&](const Rectangle& rectangle) {
            return rectangle.color.red == color.red && rectangle.color.green == color.green &&
                   rectangle.color.blue == color.blue && rectangle.position.x >= origin.x &&
                   rectangle.position.y >= origin.y &&
                   rectangle.position.x + rectangle.width <= origin.x + width &&
                   rectangle.position.y + rectangle.height <= origin.y + height;
        });
    }
    struct Rectangle {
        core::PixelPosition position;
        std::int32_t width;
        std::int32_t height;
        core::RgbColor color;
    };
    std::optional<core::PixelPosition> textAt(const char* value, std::int32_t y) const {
        for (auto item = drawnTexts.rbegin(); item != drawnTexts.rend(); ++item) {
            if (item->value == value && item->position.y == y)
                return item->position;
        }
        return std::nullopt;
    }
    std::vector<core::SlideDirection> transitions;
    std::vector<Rectangle> rectangles;
    std::vector<std::string> texts;
    struct DrawnText {
        core::PixelPosition position;
        std::string value;
    };
    std::vector<DrawnText> drawnTexts;
    int frames = 0;
    int presentations = 0;
    bool dirty = false;
};

class AudioAdapter final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t) override { return true; }
    void end() override {}
    void setVolume(std::uint8_t) override {}
    bool isPlaying() const override { return false; }
    bool play(const core::AudioClip&) override { return true; }
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
    connectivity::WifiAdapterResult connect(const connectivity::WifiNetworkConfig& value) override {
        lastConfig = value;
        ++connectCalls;
        return {};
    }
    connectivity::WifiAdapterResult disconnect() override {
        ++disconnectCalls;
        return {};
    }
    connectivity::WifiAdapterState state() const override { return adapterState; }
    std::optional<std::int32_t> signalStrengthDbm() const override { return rssi; }
    connectivity::WifiAdapterState adapterState = connectivity::WifiAdapterState::Disconnected;
    std::optional<std::int32_t> rssi;
    connectivity::WifiNetworkConfig lastConfig;
    int connectCalls = 0;
    int disconnectCalls = 0;
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
    Fixture() {
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(audio.start() == services::AudioResult::Success);
        TEST_ASSERT_TRUE(bus.registerHandler("audio.volume.step", audio) ==
                         core::RegistrationResult::Registered);
        for (const auto* id : {"network.set-enabled", "network.configure", "network.forget"})
            TEST_ASSERT_TRUE(bus.registerHandler(id, network) ==
                             core::RegistrationResult::Registered);
    }
    void storeWifi(bool enabled, std::string ssid = "HOME",
                   std::string passphrase = "recognizable-secret") {
        auto value = config.value();
        value.wifi = {enabled, std::move(ssid), std::move(passphrase)};
        TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
    }
    void reachWifi(connectivity::WifiAdapterState state,
                   std::optional<std::int32_t> signal = std::nullopt) {
        if (!config.value().wifi.enabled)
            storeWifi(true);
        TEST_ASSERT_TRUE(network.start() == services::NetworkResult::Success);
        wifiAdapter.adapterState = state;
        wifiAdapter.rssi = signal;
        network.update(std::chrono::milliseconds(1));
    }
};

const core::InputEvent tab{core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {}};
const core::InputEvent enter{core::InputEventType::NamedKey, 0, core::NamedKey::Enter, {}};
const core::InputEvent escape{core::InputEventType::NamedKey, 0, core::NamedKey::Escape, {}};
const core::InputEvent backtick{core::InputEventType::PrintableCharacter, '`', {}, {}};
const core::InputEvent down{core::InputEventType::NamedKey, 0, core::NamedKey::Down, {}};
const core::InputEvent backspace{core::InputEventType::NamedKey, 0, core::NamedKey::Backspace, {}};
core::InputEvent character(char c) { return {core::InputEventType::PrintableCharacter, c, {}, {}}; }

void type(apps::WiFiSettings& ui, const std::string& text) {
    core::InputEvents events;
    events.reserve(text.size());
    for (const auto c : text)
        events.push_back(character(c));
    ui.update(events);
}

void test_home_wifi_indicator_mapping() {
    services::WifiStatusSnapshot snapshot;
    TEST_ASSERT_TRUE(apps::homeWifiIndicator(snapshot) == apps::HomeWifiIndicator::HollowQuiet);
    snapshot.configured = true;
    TEST_ASSERT_TRUE(apps::homeWifiIndicator(snapshot) == apps::HomeWifiIndicator::FilledPale);
    snapshot.enabled = true;
    snapshot.connection = services::WifiConnectionStatus::Connecting;
    TEST_ASSERT_TRUE(apps::homeWifiIndicator(snapshot) == apps::HomeWifiIndicator::FilledBlue);
    snapshot.connection = services::WifiConnectionStatus::Connected;
    TEST_ASSERT_TRUE(apps::homeWifiIndicator(snapshot) == apps::HomeWifiIndicator::FilledLeaf);
    snapshot.connection = services::WifiConnectionStatus::Error;
    TEST_ASSERT_TRUE(apps::homeWifiIndicator(snapshot) == apps::HomeWifiIndicator::FilledVermilion);
}

void test_home_renders_semantic_wifi_dot_without_status_text() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.hostSettings,
                                 f.wifiSettings, f.audio, f.miniApps, f.capabilities);
    shell.update({});
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
    TEST_ASSERT_FALSE(f.display.shows("OFFLINE"));
    TEST_ASSERT_FALSE(f.display.shows("ONLINE"));
    TEST_ASSERT_TRUE(
        f.display.regionHasColor(apps::homeWifiDotPosition, 5, 5, core::palette::ordinal));
    TEST_ASSERT_TRUE(f.display.hasColor(core::palette::ink));

    f.storeWifi(false);
    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({});
    TEST_ASSERT_FALSE(f.display.shows("HOME"));
    TEST_ASSERT_TRUE(
        f.display.regionHasColor(apps::homeWifiDotPosition, 5, 5, core::palette::pale));

    f.reachWifi(connectivity::WifiAdapterState::Connecting);
    f.display.rectangles.clear();
    shell.update({});
    TEST_ASSERT_TRUE(
        f.display.regionHasColor(apps::homeWifiDotPosition, 5, 5, core::palette::blue));

    f.wifiAdapter.adapterState = connectivity::WifiAdapterState::Connected;
    f.wifiAdapter.rssi = -54;
    f.network.update(std::chrono::milliseconds(1));
    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({});
    TEST_ASSERT_TRUE(
        f.display.regionHasColor(apps::homeWifiDotPosition, 5, 5, core::palette::leaf));
    TEST_ASSERT_FALSE(f.display.shows("HOME"));
    TEST_ASSERT_FALSE(f.display.shows("-54 dBm"));
    TEST_ASSERT_FALSE(f.display.shows("CONNECTED"));

    f.wifiAdapter.adapterState = connectivity::WifiAdapterState::Error;
    f.network.update(std::chrono::milliseconds(1));
    f.display.rectangles.clear();
    shell.update({});
    TEST_ASSERT_TRUE(
        f.display.regionHasColor(apps::homeWifiDotPosition, 5, 5, core::palette::vermilion));
}

void test_home_wifi_redraws_only_on_semantic_changes() {
    Fixture f;
    f.reachWifi(connectivity::WifiAdapterState::Connected, -40);
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.hostSettings,
                                 f.wifiSettings, f.audio, f.miniApps, f.capabilities);
    shell.update({});
    const auto presentations = f.display.presentations;
    f.display.rectangles.clear();
    f.wifiAdapter.rssi = -20;
    shell.update({});
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
    TEST_ASSERT_TRUE(f.display.rectangles.empty());

    f.display.rectangles.clear();
    f.display.texts.clear();
    shell.update({}, std::chrono::milliseconds(0), 81);
    TEST_ASSERT_TRUE(f.display.shows("81%"));
    for (const auto& rectangle : f.display.rectangles)
        TEST_ASSERT_TRUE(rectangle.position.y + rectangle.height <= 24);
    TEST_ASSERT_FALSE(
        f.display.regionHasColor(apps::homeWifiDotPosition, 5, 5, core::palette::leaf));

    f.display.rectangles.clear();
    f.wifiAdapter.adapterState = connectivity::WifiAdapterState::Error;
    f.network.update(std::chrono::milliseconds(1));
    shell.update({});
    TEST_ASSERT_TRUE(f.display.regionHasColor(apps::homeWifiRegion, apps::homeWifiRegionWidth,
                                              apps::homeWifiRegionHeight,
                                              core::palette::vermilion));
}

void test_settings_opens_wifi_forward_and_returns_backward() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.hostSettings,
                                 f.wifiSettings, f.audio, f.miniApps, f.capabilities);
    shell.update({tab});
    TEST_ASSERT_TRUE(f.display.shows("Wi-Fi"));
    f.display.transitions.clear();
    shell.update({down, enter});
    TEST_ASSERT_EQUAL_UINT(1, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Forward);
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("NOT CONFIGURED"));
    TEST_ASSERT_TRUE(f.display.shows("Configure network"));
    TEST_ASSERT_FALSE(f.display.shows("Forget network"));
    TEST_ASSERT_FALSE(f.display.shows("ESC CANCEL"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER NEXT"));

    f.display.transitions.clear();
    shell.update({escape});
    TEST_ASSERT_EQUAL_UINT(1, f.display.transitions.size());
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
    TEST_ASSERT_TRUE(f.display.shows("SETTINGS"));
    TEST_ASSERT_TRUE(f.display.shows("Wi-Fi"));
}

void test_wifi_settings_shows_connected_status_and_rssi() {
    Fixture f;
    f.reachWifi(connectivity::WifiAdapterState::Connected, -54);
    f.wifiSettings.activate();
    f.wifiSettings.update({});
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("CONNECTED"));
    TEST_ASSERT_TRUE(f.display.shows("ON"));
    TEST_ASSERT_TRUE(f.display.shows("HOME"));
    TEST_ASSERT_TRUE(f.display.shows("-54 dBm"));
    TEST_ASSERT_TRUE(f.display.shows("Change network"));
    TEST_ASSERT_TRUE(f.display.shows("Forget network"));
    const auto connected = f.display.textAt("CONNECTED", 6);
    TEST_ASSERT_TRUE(connected.has_value());
    TEST_ASSERT_EQUAL_INT32(core::rightAlignedTextX("CONNECTED"), connected->x);
}

void test_wifi_settings_hides_rssi_when_disconnected_and_toggle_persists() {
    Fixture f;
    f.storeWifi(false);
    f.wifiSettings.activate();
    f.wifiSettings.update({});
    TEST_ASSERT_TRUE(f.display.shows("OFF"));
    TEST_ASSERT_FALSE(f.display.shows("Signal"));
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.network.status().enabled);
    TEST_ASSERT_TRUE(f.display.shows("CONNECTING"));
    TEST_ASSERT_EQUAL(1, f.wifiAdapter.connectCalls);
}

void test_unconfigured_wifi_opens_configure_on_the_first_enter() {
    Fixture f;
    f.wifiSettings.activate();
    f.wifiSettings.update({});
    TEST_ASSERT_TRUE(f.display.shows("Configure network"));
    TEST_ASSERT_FALSE(f.display.shows("Wi-Fi"));
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_TRUE(f.display.shows("ESC CANCEL"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER NEXT"));
}

void test_focus_keeps_change_and_forget_when_signal_row_appears() {
    Fixture f;
    f.reachWifi(connectivity::WifiAdapterState::Connected, -40);
    f.wifiSettings.activate();
    f.wifiSettings.update({down, down, down});
    TEST_ASSERT_TRUE(f.display.shows("Change network"));
    f.wifiAdapter.adapterState = connectivity::WifiAdapterState::Error;
    f.network.update(std::chrono::milliseconds(1));
    f.wifiSettings.update({});
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("ERROR"));
    TEST_ASSERT_TRUE(f.display.shows("Wi-Fi connection failed"));
    TEST_ASSERT_FALSE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_FALSE(f.display.shows("FORGET NETWORK?"));
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_FALSE(f.display.shows("FORGET NETWORK?"));

    Fixture forget;
    forget.storeWifi(false);
    forget.wifiSettings.activate();
    forget.wifiSettings.update({down, down, down});
    forget.reachWifi(connectivity::WifiAdapterState::Connected, -22);
    forget.wifiSettings.update({});
    TEST_ASSERT_TRUE(forget.display.shows("Signal"));
    forget.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(forget.display.shows("FORGET NETWORK?"));
    TEST_ASSERT_FALSE(forget.display.shows("NETWORK NAME"));
}

void test_editor_keeps_long_credentials_on_screen() {
    Fixture f;
    f.wifiSettings.activate();
    f.wifiSettings.update({enter});
    const std::string ssid(services::NetworkService::maximumSsidLength, 'A');
    type(f.wifiSettings, ssid + "Z");
    TEST_ASSERT_TRUE(f.display.shows((ssid + "_").c_str()));
    f.wifiSettings.update({backspace});
    TEST_ASSERT_TRUE(f.display.shows((std::string(31, 'A') + "_").c_str()));
    type(f.wifiSettings, "A");
    f.wifiSettings.update({enter});

    type(f.wifiSettings, std::string(63, 'b'));
    TEST_ASSERT_TRUE(f.display.shows((std::string(37, '*') + "b_").c_str()));
    f.wifiSettings.update({}, apps::WiFiSettings::passphraseRevealDuration);
    TEST_ASSERT_TRUE(f.display.shows((std::string(38, '*') + "_").c_str()));
    f.wifiSettings.update({backspace});
    TEST_ASSERT_TRUE(f.display.shows((std::string(38, '*') + "_").c_str()));
    type(f.wifiSettings, "b");
    TEST_ASSERT_TRUE(f.display.shows((std::string(37, '*') + "b_").c_str()));
    TEST_ASSERT_FALSE(
        f.display.shows("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.network.status().configured);

    Fixture psk;
    psk.wifiSettings.activate();
    psk.wifiSettings.update({enter});
    type(psk.wifiSettings, "Hidden");
    psk.wifiSettings.update({enter});
    type(psk.wifiSettings, std::string(64, 'a'));
    TEST_ASSERT_TRUE(psk.display.shows((std::string(37, '*') + "a_").c_str()));
    psk.wifiSettings.update({}, apps::WiFiSettings::passphraseRevealDuration);
    TEST_ASSERT_TRUE(psk.display.shows((std::string(38, '*') + "_").c_str()));
    psk.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(psk.network.status().configured);
    TEST_ASSERT_EQUAL_STRING("Hidden", psk.network.status().ssid.c_str());
}

void test_connectivity_error_stays_on_status_without_changing_focus() {
    Fixture f;
    f.reachWifi(connectivity::WifiAdapterState::Connecting);
    f.wifiSettings.activate();
    f.wifiSettings.update({});
    TEST_ASSERT_TRUE(f.display.shows("CONNECTING"));
    f.wifiAdapter.adapterState = connectivity::WifiAdapterState::Error;
    f.network.update(std::chrono::milliseconds(1));
    f.wifiSettings.update({});
    TEST_ASSERT_TRUE(f.network.status().lastResult == services::NetworkResult::ConnectivityError);
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("ERROR"));
    TEST_ASSERT_TRUE(f.display.shows("Wi-Fi connection failed"));
    TEST_ASSERT_TRUE(f.display.shows("ON"));
    TEST_ASSERT_FALSE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_FALSE(f.display.shows("FORGET NETWORK?"));
    f.wifiSettings.update({enter});
    TEST_ASSERT_FALSE(f.network.status().enabled);
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_FALSE(f.display.shows("NETWORK NAME"));
}

void test_change_network_storage_failure_keeps_the_published_network() {
    Fixture f;
    f.storeWifi(false);
    f.wifiSettings.activate();
    f.wifiSettings.update({down, down, enter});
    type(f.wifiSettings, "Guest");
    f.wifiSettings.update({enter});
    type(f.wifiSettings, "recognizable-secret");
    f.memory.writeError = true;
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.network.status().lastResult == services::NetworkResult::StorageError);
    TEST_ASSERT_EQUAL_STRING("HOME", f.network.status().ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("HOME", f.config.value().wifi.ssid.c_str());
    TEST_ASSERT_TRUE(f.display.shows("PASSWORD"));
    TEST_ASSERT_TRUE(f.display.shows("Settings could not be saved"));
}

void test_manual_configure_masks_passphrase_and_clears_drafts() {
    Fixture f;
    f.wifiSettings.activate();
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_TRUE(f.display.shows("ESC CANCEL"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER NEXT"));
    type(f.wifiSettings, "O");
    TEST_ASSERT_TRUE(f.display.shows("O_"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER NEXT"));
    f.wifiSettings.update({backspace});
    TEST_ASSERT_TRUE(f.display.shows("_"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER NEXT"));
    type(f.wifiSettings, "Office");
    TEST_ASSERT_TRUE(f.display.shows("Office_"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER NEXT"));
    f.wifiSettings.update({character(';'), character('.'), backspace});
    TEST_ASSERT_TRUE(f.display.shows("Office;_"));
    TEST_ASSERT_FALSE(f.network.status().configured);
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("PASSWORD"));
    TEST_ASSERT_TRUE(f.display.shows("ESC CANCEL"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER CONNECT"));
    type(f.wifiSettings, "recognizable-secret");
    TEST_ASSERT_TRUE(f.display.shows("******************t_"));
    TEST_ASSERT_FALSE(f.display.shows("recognizable-secret"));
    TEST_ASSERT_FALSE(f.display.shows("recognizable-secret_"));
    f.wifiSettings.update({}, apps::WiFiSettings::passphraseRevealDuration);
    TEST_ASSERT_TRUE(f.display.shows("*******************_"));
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.network.status().configured);
    TEST_ASSERT_EQUAL_STRING("Office;", f.network.status().ssid.c_str());
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_FALSE(f.display.shows("PASSWORD"));
    TEST_ASSERT_FALSE(f.display.shows("recognizable-secret"));
}

void test_passphrase_reveals_last_character_then_masks() {
    Fixture f;
    f.wifiSettings.activate();
    f.wifiSettings.update({enter});
    type(f.wifiSettings, "Open");
    f.wifiSettings.update({enter});
    type(f.wifiSettings, "ab");
    TEST_ASSERT_TRUE(f.display.shows("*b_"));
    TEST_ASSERT_FALSE(f.display.shows("ab_"));
    f.wifiSettings.update({}, apps::WiFiSettings::passphraseRevealDuration -
                                  std::chrono::milliseconds(1));
    TEST_ASSERT_TRUE(f.display.shows("*b_"));
    f.wifiSettings.update({}, std::chrono::milliseconds(1));
    TEST_ASSERT_TRUE(f.display.shows("**_"));
    TEST_ASSERT_FALSE(f.display.shows("*b_"));
    type(f.wifiSettings, "c");
    TEST_ASSERT_TRUE(f.display.shows("**c_"));
    f.wifiSettings.update({backspace});
    TEST_ASSERT_TRUE(f.display.shows("**_"));
}

void test_empty_ssid_stays_in_editor_and_escape_cancels() {
    Fixture f;
    f.storeWifi(false, "HOME", "recognizable-secret");
    f.wifiSettings.activate();
    f.wifiSettings.update({down, down, enter});
    TEST_ASSERT_TRUE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_TRUE(f.display.shows("ESC CANCEL"));
    TEST_ASSERT_FALSE(f.display.shows("ENTER NEXT"));
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_EQUAL_STRING("HOME", f.network.status().ssid.c_str());
    type(f.wifiSettings, "O");
    TEST_ASSERT_TRUE(f.display.shows("ENTER NEXT"));
    f.wifiSettings.update({backspace});
    TEST_ASSERT_FALSE(f.display.shows("ENTER NEXT"));
    type(f.wifiSettings, "Guest");
    f.wifiSettings.update({backtick});
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_EQUAL_STRING("HOME", f.network.status().ssid.c_str());
    TEST_ASSERT_FALSE(f.display.shows("Guest_"));
}

void test_open_network_and_invalid_configuration_keep_editor_usable() {
    Fixture f;
    f.wifiSettings.activate();
    f.wifiSettings.update({enter});
    type(f.wifiSettings, "OpenNet");
    f.wifiSettings.update({enter, enter});
    TEST_ASSERT_TRUE(f.network.status().configured);
    TEST_ASSERT_EQUAL_STRING("OpenNet", f.network.status().ssid.c_str());

    f.wifiSettings.update({down, down, enter});
    type(f.wifiSettings, "Bad");
    f.wifiSettings.update({enter});
    type(f.wifiSettings, "short");
    f.wifiSettings.update({enter});
    TEST_ASSERT_TRUE(f.display.shows("PASSWORD"));
    TEST_ASSERT_TRUE(f.display.shows("Check network settings"));
    TEST_ASSERT_EQUAL_STRING("OpenNet", f.network.status().ssid.c_str());
}

void test_forget_requires_confirmation_and_escape_keeps_network() {
    Fixture f;
    f.storeWifi(false);
    f.wifiSettings.activate();
    f.wifiSettings.update({});
    TEST_ASSERT_TRUE(f.display.shows("Forget network"));
    f.wifiSettings.update({down, down, down, enter});
    TEST_ASSERT_TRUE(f.display.shows("FORGET NETWORK?"));
    TEST_ASSERT_TRUE(f.display.shows("HOME"));
    TEST_ASSERT_TRUE(f.display.shows("ESC CANCEL"));
    TEST_ASSERT_TRUE(f.display.shows("ENTER FORGET"));
    f.wifiSettings.update({backtick});
    TEST_ASSERT_TRUE(f.network.status().configured);
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    f.wifiSettings.update({down, down, down, enter, enter});
    TEST_ASSERT_FALSE(f.network.status().configured);
    TEST_ASSERT_TRUE(f.display.shows("NOT CONFIGURED"));
    TEST_ASSERT_TRUE(f.display.shows("Configure network"));
}

void test_storage_failure_leaves_configuration_and_shows_error() {
    Fixture f;
    f.storeWifi(false);
    f.memory.writeError = true;
    f.wifiSettings.activate();
    f.wifiSettings.update({enter});
    TEST_ASSERT_FALSE(f.network.status().enabled);
    TEST_ASSERT_TRUE(f.network.status().lastResult == services::NetworkResult::StorageError);
    TEST_ASSERT_TRUE(f.display.shows("Settings could not be saved"));
    TEST_ASSERT_EQUAL_STRING("HOME", f.config.value().wifi.ssid.c_str());
    TEST_ASSERT_FALSE(f.config.value().wifi.enabled);
}

void test_editor_tab_does_not_submit_and_shell_returns_to_settings() {
    Fixture f;
    apps::ApplicationShell shell(f.hosts, f.network, f.bus, f.display, f.hostSettings,
                                 f.wifiSettings, f.audio, f.miniApps, f.capabilities);
    shell.update({tab, down, enter, enter});
    TEST_ASSERT_TRUE(f.display.shows("NETWORK NAME"));
    type(f.wifiSettings, "KeepMe");
    shell.update({tab});
    TEST_ASSERT_TRUE(f.display.shows("NETWORK NAME"));
    TEST_ASSERT_FALSE(f.network.status().configured);
    shell.update({escape});
    TEST_ASSERT_TRUE(f.display.shows("WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("NOT CONFIGURED"));
    f.display.transitions.clear();
    shell.update({tab});
    TEST_ASSERT_TRUE(f.display.shows("SETTINGS"));
    TEST_ASSERT_TRUE(f.display.transitions.back() == core::SlideDirection::Backward);
}

void test_wifi_settings_does_not_repaint_identical_status() {
    Fixture f;
    f.reachWifi(connectivity::WifiAdapterState::Connected, -30);
    f.wifiSettings.activate();
    f.wifiSettings.update({});
    const auto frames = f.display.frames;
    const auto presentations = f.display.presentations;
    f.wifiSettings.update({});
    TEST_ASSERT_EQUAL(frames, f.display.frames);
    TEST_ASSERT_EQUAL(presentations, f.display.presentations);
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_home_wifi_indicator_mapping);
    RUN_TEST(test_home_renders_semantic_wifi_dot_without_status_text);
    RUN_TEST(test_home_wifi_redraws_only_on_semantic_changes);
    RUN_TEST(test_settings_opens_wifi_forward_and_returns_backward);
    RUN_TEST(test_wifi_settings_shows_connected_status_and_rssi);
    RUN_TEST(test_wifi_settings_hides_rssi_when_disconnected_and_toggle_persists);
    RUN_TEST(test_unconfigured_wifi_opens_configure_on_the_first_enter);
    RUN_TEST(test_focus_keeps_change_and_forget_when_signal_row_appears);
    RUN_TEST(test_editor_keeps_long_credentials_on_screen);
    RUN_TEST(test_connectivity_error_stays_on_status_without_changing_focus);
    RUN_TEST(test_change_network_storage_failure_keeps_the_published_network);
    RUN_TEST(test_manual_configure_masks_passphrase_and_clears_drafts);
    RUN_TEST(test_passphrase_reveals_last_character_then_masks);
    RUN_TEST(test_empty_ssid_stays_in_editor_and_escape_cancels);
    RUN_TEST(test_open_network_and_invalid_configuration_keep_editor_usable);
    RUN_TEST(test_forget_requires_confirmation_and_escape_keeps_network);
    RUN_TEST(test_storage_failure_leaves_configuration_and_shows_error);
    RUN_TEST(test_editor_tab_does_not_submit_and_shell_returns_to_settings);
    RUN_TEST(test_wifi_settings_does_not_repaint_identical_status);
    return UNITY_END();
}
