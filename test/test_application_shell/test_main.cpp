#include "apps/hosts/host_settings.h"
#include "apps/network/wifi_settings.h"
#include "apps/runtime/mini_app_runtime.h"
#include "apps/shell/application_shell.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
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
    void clear(core::RgbColor) override {
        ++frames;
        texts.clear();
        dirty = true;
    }
    void fillRectangle(core::PixelPosition position, std::int32_t width, std::int32_t height,
                       core::RgbColor) override {
        TEST_ASSERT_TRUE(position.x >= 0 && position.y >= 0 && width > 0 && height > 0);
        TEST_ASSERT_TRUE(position.x + width <= 240 && position.y + height <= 135);
        dirty = true;
    }
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
    int frames = 0;
    int presentations = 0;
    bool dirty = false;
};

class AudioAdapter final : public core::IAudioAdapter {
  public:
    bool begin(std::uint8_t) override { return true; }
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
    void onDeactivate() override { ++deactivateCount; }
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override {
        ++updateCount;
        lastInput = input;
        lastElapsed = elapsed;
        if (display != nullptr)
            display->texts = {"MINI APP"};
    }
    Display* display = nullptr;
    int activateCount = 0;
    int deactivateCount = 0;
    int updateCount = 0;
    core::InputEvents lastInput;
    std::chrono::milliseconds lastElapsed{0};
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
                                      audio, miniApps);
    }
    void registerApp(const char* id, std::vector<std::string> required = {}) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(core::AppRegistrationResult::Registered),
                                static_cast<unsigned int>(appRegistry.registerApp(
                                    {id, id, "", std::string(id) + "/home", std::move(required)})));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned int>(apps::MiniAppInstanceRegistrationResult::Registered),
            static_cast<unsigned int>(miniApps.registerInstance(id, app)));
    }
};

const core::InputEvent tab{core::InputEventType::NamedKey, 0, core::NamedKey::Tab, {}};

void test_idle_runtime_preserves_home_and_settings() {
    Fixture f;
    auto shell = f.makeShell();

    shell.update({});
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
    TEST_ASSERT_TRUE(f.display.shows("SELECTED HOST"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));

    shell.update({tab});
    TEST_ASSERT_TRUE(f.display.shows("SETTINGS"));
    TEST_ASSERT_TRUE(f.display.shows("Bluetooth"));
    TEST_ASSERT_TRUE(f.display.shows("Wi-Fi"));
    TEST_ASSERT_TRUE(f.display.shows("Sound volume"));
    TEST_ASSERT_EQUAL_INT(0, f.app.updateCount);
}

void test_active_mini_app_receives_scheduled_update_instead_of_shell_input() {
    Fixture f;
    f.registerApp("weather");
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned int>(f.miniApps.activate("weather")));
    auto shell = f.makeShell();

    shell.update({tab}, std::chrono::milliseconds(41));

    TEST_ASSERT_EQUAL_INT(1, f.app.updateCount);
    TEST_ASSERT_EQUAL_UINT32(1, f.app.lastInput.size());
    TEST_ASSERT_TRUE(f.app.lastInput.front().namedKey == core::NamedKey::Tab);
    TEST_ASSERT_EQUAL_INT64(41, f.app.lastElapsed.count());
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
    TEST_ASSERT_TRUE(f.miniApps.hasActiveApp());
}

void test_explicit_runtime_deactivation_returns_shell_to_built_in_ui() {
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

    TEST_ASSERT_TRUE(f.display.shows("SETTINGS"));
    TEST_ASSERT_EQUAL_INT(1, f.app.updateCount);
}

void test_explicit_runtime_deactivation_invalidates_home_before_idle_update() {
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
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
    TEST_ASSERT_TRUE(f.display.shows("SELECTED HOST"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
}

void test_capability_loss_returns_shell_to_home_without_forwarding_input() {
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
    TEST_ASSERT_TRUE(f.display.shows("--:--"));
    TEST_ASSERT_TRUE(f.display.shows("SELECTED HOST"));
    TEST_ASSERT_FALSE(f.display.shows("SETTINGS"));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_runtime_preserves_home_and_settings);
    RUN_TEST(test_active_mini_app_receives_scheduled_update_instead_of_shell_input);
    RUN_TEST(test_explicit_runtime_deactivation_returns_shell_to_built_in_ui);
    RUN_TEST(test_explicit_runtime_deactivation_invalidates_home_before_idle_update);
    RUN_TEST(test_capability_loss_returns_shell_to_home_without_forwarding_input);
    return UNITY_END();
}
