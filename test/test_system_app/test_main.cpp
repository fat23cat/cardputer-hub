#include "apps/runtime/mini_app_runtime.h"
#include "apps/system/system_app.h"
#include "core/app_registry/app_registry.h"
#include "core/audio/audio_adapter.h"
#include "core/capabilities/capability_registry.h"
#include "core/lifecycle/build_info.h"
#include "core/power/battery_adapter.h"
#include "services/network/network_service.h"

#include <algorithm>
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
        bytes = value;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
    core::StorageBytes bytes;
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

class BatteryAdapter final : public core::IBatteryAdapter {
  public:
    int readPercent() override { return value; }
    int value = -1;
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
    connectivity::BluetoothBondListResult bonds() override {
        return {connectivity::BluetoothBondListStatus::Success, bonded};
    }
    std::vector<connectivity::BluetoothBondReference> bonded;
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
    connectivity::WifiAdapterState state() const override { return adapterState; }
    std::optional<std::int32_t> signalStrengthDbm() const override { return rssi; }
    connectivity::WifiAdapterState adapterState = connectivity::WifiAdapterState::Disconnected;
    std::optional<std::int32_t> rssi;
};

struct Fixture {
    Memory memory;
    core::Storage storage{memory};
    services::ConfigurationService config{storage};
    BatteryAdapter batteryAdapter;
    services::BatteryService battery{batteryAdapter};
    BluetoothAdapter bluetoothAdapter;
    connectivity::BluetoothService bluetooth{bluetoothAdapter};
    services::HostService hosts{bluetooth, config};
    WifiAdapter wifiAdapter;
    connectivity::WiFiService wifi{wifiAdapter};
    services::NetworkService network{wifi, config};
    Display display;
    apps::SystemApp app{battery, hosts, network, display};

    Fixture() { TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success); }

    void setHost(const char* name, bool enabled) {
        auto value = config.value();
        connectivity::BluetoothBondReference bond{};
        bond.bytes[0] = 1;
        value.host.hosts = {{1, name, bond, std::nullopt, {}, std::nullopt}};
        value.host.activeHost = 1;
        value.host.nextHostId = 2;
        bluetoothAdapter.bonded = {bond};
        TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(hosts.start() == services::HostResult::Success);
        if (enabled)
            TEST_ASSERT_TRUE(hosts.setEnabled(true) == services::HostResult::Success);
    }

    void setWifi(bool enabled, const char* ssid, connectivity::WifiAdapterState state) {
        auto value = config.value();
        value.wifi = {enabled, ssid, "recognizable-secret"};
        TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(network.start() == services::NetworkResult::Success);
        wifiAdapter.adapterState = state;
        network.update(std::chrono::milliseconds(1));
    }
};

void test_battery_values() {
    Fixture f;
    f.app.onActivate();
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("--%"));
    f.batteryAdapter.value = 81;
    f.battery.update(std::chrono::milliseconds(0));
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("81%"));
}

void test_host_name_and_status_changes() {
    Fixture f;
    f.app.onActivate();
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("NONE"));
    TEST_ASSERT_TRUE(f.display.shows("OFF"));
    TEST_ASSERT_TRUE(f.display.shows("BLUETOOTH"));
    f.setHost("Work MacBook", false);
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("WORK MACBOOK"));
    TEST_ASSERT_TRUE(f.display.shows("OFF"));
    f.setHost("Work MacBook", true);
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("CONNECTING"));
    TEST_ASSERT_TRUE(f.display.shows("ON"));
}

void test_wifi_states_and_network_name() {
    Fixture f;
    f.app.onActivate();
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("OFF"));
    TEST_ASSERT_TRUE(f.display.shows("NONE"));

    f.setWifi(false, "MY-WIFI", connectivity::WifiAdapterState::Disconnected);
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("MY-WIFI"));
    TEST_ASSERT_TRUE(f.display.shows("OFF"));

    f.setWifi(true, "MY-WIFI", connectivity::WifiAdapterState::Connecting);
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("CONNECTING"));

    f.wifiAdapter.adapterState = connectivity::WifiAdapterState::Connected;
    f.network.update(std::chrono::milliseconds(1));
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("CONNECTED"));

    f.wifiAdapter.adapterState = connectivity::WifiAdapterState::Error;
    f.network.update(std::chrono::milliseconds(1));
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("ERROR"));
}

void test_wifi_status_text_distinguishes_enabled_and_configured() {
    services::WifiStatusSnapshot status;
    TEST_ASSERT_EQUAL_STRING("OFF", apps::systemWifiStatusText(status));

    status.enabled = true;
    TEST_ASSERT_EQUAL_STRING("ON", apps::systemWifiStatusText(status));

    status.configured = true;
    status.connection = services::WifiConnectionStatus::Connecting;
    TEST_ASSERT_EQUAL_STRING("CONNECTING", apps::systemWifiStatusText(status));

    status.connection = services::WifiConnectionStatus::Connected;
    TEST_ASSERT_EQUAL_STRING("CONNECTED", apps::systemWifiStatusText(status));

    status.connection = services::WifiConnectionStatus::Error;
    TEST_ASSERT_EQUAL_STRING("ERROR", apps::systemWifiStatusText(status));

    status.connection = services::WifiConnectionStatus::Off;
    TEST_ASSERT_EQUAL_STRING("ON", apps::systemWifiStatusText(status));

    status.enabled = false;
    TEST_ASSERT_EQUAL_STRING("OFF", apps::systemWifiStatusText(status));
}

void test_unchanged_snapshots_do_not_repaint() {
    Fixture f;
    f.batteryAdapter.value = 81;
    f.battery.update(std::chrono::milliseconds(0));
    f.app.onActivate();
    f.app.update({}, {});
    const auto frames = f.display.frames;
    f.app.update({}, {});
    TEST_ASSERT_EQUAL_INT(frames, f.display.frames);
}

void test_one_value_change_updates_presentation() {
    Fixture f;
    f.batteryAdapter.value = 81;
    f.battery.update(std::chrono::milliseconds(0));
    f.app.onActivate();
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("81%"));
    f.batteryAdapter.value = 40;
    f.battery.update(std::chrono::milliseconds(5000));
    f.app.update({}, {});
    TEST_ASSERT_TRUE(f.display.shows("40%"));
}

void test_lifecycle_through_runtime() {
    Fixture f;
    f.batteryAdapter.value = 50;
    f.battery.update(std::chrono::milliseconds(0));
    core::AppRegistry apps;
    core::CapabilityRegistry capabilities;
    apps::MiniAppRuntime runtime{apps, capabilities};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(core::AppRegistrationResult::Registered),
        static_cast<unsigned>(apps.registerApp({"system", "SYSTEM", "system", "system/home", {}})));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(apps::MiniAppInstanceRegistrationResult::Registered),
        static_cast<unsigned>(runtime.registerInstance("system", f.app)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MiniAppActivationResult::Activated),
                            static_cast<unsigned>(runtime.activate("system")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MiniAppUpdateResult::Updated),
                            static_cast<unsigned>(runtime.update({}, {})));
    TEST_ASSERT_TRUE(f.display.shows("SYSTEM"));
    TEST_ASSERT_TRUE(f.display.shows("50%"));
    TEST_ASSERT_TRUE(f.display.shows(core::firmwareBuildInfo().version));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(apps::MiniAppDeactivationResult::Deactivated),
                            static_cast<unsigned>(runtime.deactivate()));
    TEST_ASSERT_FALSE(runtime.hasActiveApp());
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_battery_values);
    RUN_TEST(test_host_name_and_status_changes);
    RUN_TEST(test_wifi_states_and_network_name);
    RUN_TEST(test_wifi_status_text_distinguishes_enabled_and_configured);
    RUN_TEST(test_unchanged_snapshots_do_not_repaint);
    RUN_TEST(test_one_value_change_updates_presentation);
    RUN_TEST(test_lifecycle_through_runtime);
    return UNITY_END();
}
