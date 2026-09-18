#include <unity.h>

#include <algorithm>
#include <utility>

#include "services/configuration/configuration_service.h"

using namespace cardputer_hub;

namespace {
class MemoryStorage final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress&) override {
        return {readError       ? core::StorageReadStatus::BackendError
                : bytes.empty() ? core::StorageReadStatus::NotFound
                                : core::StorageReadStatus::Found,
                bytes};
    }
    core::StorageWriteStatus write(const core::StorageAddress&,
                                   const core::StorageBytes& data) override {
        ++writes;
        if (writeError)
            return core::StorageWriteStatus::BackendError;
        bytes = data;
        return core::StorageWriteStatus::Stored;
    }
    core::StorageRemoveStatus remove(const core::StorageAddress&) override {
        return core::StorageRemoveStatus::NotFound;
    }
    core::StorageBytes bytes;
    bool readError = false;
    bool writeError = false;
    int writes = 0;
};

services::SystemConfiguration configuration() {
    services::SystemConfiguration value;
    connectivity::BluetoothBondReference a{}, b{};
    a.bytes[0] = 1;
    b.bytes[0] = 2;
    value.host.hosts = {{7, "Office laptop", a, std::nullopt, {}, std::nullopt},
                        {9, "Travel laptop", b, std::nullopt, {}, std::nullopt}};
    value.host.nextHostId = 10;
    value.host.activeHost = 9;
    value.host.bluetoothEnabled = true;
    value.soundVolume = 80;
    value.host.hosts[0].platform = "macos";
    value.host.hosts[0].capabilities = {"app.activate", "app.active"};
    value.host.hosts[0].mappingTemplate = "macos.default";
    return value;
}

services::SystemConfiguration maximalConfiguration() {
    services::SystemConfiguration value;
    const auto platform = std::string{"p"} + std::string(31, 'a');
    const auto mappingTemplate = std::string{"m"} + std::string(31, 'a');
    for (std::uint32_t id = 1; id <= connectivity::BluetoothService::maximumBondCount; ++id) {
        connectivity::BluetoothBondReference reference{};
        reference.bytes[0] = static_cast<std::uint8_t>(id);
        services::HostProfile host{
            id,        std::string(services::ConfigurationService::maximumNameLength, 'H'),
            reference, platform,
            {},        mappingTemplate};
        for (std::size_t capability = 0;
             capability < services::ConfigurationService::maximumHostCapabilityCount;
             ++capability) {
            const auto suffix =
                capability < 10 ? "0" + std::to_string(capability) : std::to_string(capability);
            host.capabilities.push_back(std::string{"c"} + std::string(29, 'a') + suffix);
        }
        value.host.hosts.push_back(std::move(host));
    }
    value.host.nextHostId = static_cast<std::uint32_t>(value.host.hosts.size()) + 1;
    value.host.activeHost = value.host.hosts.back().id;
    value.host.bluetoothEnabled = true;
    value.wifi.enabled = true;
    value.wifi.ssid = std::string(32, 'S');
    value.wifi.passphrase = std::string(64, 'a');
    value.soundVolume = 100;
    return value;
}

core::StorageBytes versionOneConfiguration() {
    const auto appendInteger = [](core::StorageBytes& bytes, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    core::StorageBytes bytes{'H', 'U', 'B', 'H', 1, 1, 1};
    appendInteger(bytes, 8);
    appendInteger(bytes, 7);
    appendInteger(bytes, 7);
    const std::string name = "Office laptop";
    bytes.push_back(static_cast<std::uint8_t>(name.size()));
    bytes.insert(bytes.end(), name.begin(), name.end());
    connectivity::BluetoothBondReference reference{};
    reference.bytes[0] = 1;
    bytes.insert(bytes.end(), reference.bytes.begin(), reference.bytes.end());
    return bytes;
}

core::StorageBytes versionTwoConfiguration() {
    const auto appendInteger = [](core::StorageBytes& bytes, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    const auto appendString = [](core::StorageBytes& bytes, const std::string& value) {
        bytes.push_back(static_cast<std::uint8_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    };
    core::StorageBytes bytes{'H', 'U', 'B', 'H', 2, 1, 1};
    appendInteger(bytes, 8);
    appendInteger(bytes, 7);
    appendInteger(bytes, 7);
    appendString(bytes, "Office laptop");
    connectivity::BluetoothBondReference reference{};
    reference.bytes[0] = 1;
    bytes.insert(bytes.end(), reference.bytes.begin(), reference.bytes.end());
    appendString(bytes, "macos");
    bytes.push_back(2);
    appendString(bytes, "app.activate");
    appendString(bytes, "app.active");
    appendString(bytes, "macos.default");
    return bytes;
}

core::StorageBytes versionThreeConfiguration() {
    auto bytes = versionTwoConfiguration();
    bytes[4] = 3;
    bytes.insert(bytes.begin() + 7, 80);
    return bytes;
}

void test_selection_names_and_off_survive_reload() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService writer(storage), reader(storage);
    auto value = configuration();
    value.host.bluetoothEnabled = false;
    value.wifi = {false, "Office WiFi", "correct horse battery staple"};
    TEST_ASSERT_TRUE(writer.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(writer.save(value) == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT(2, reader.value().host.hosts.size());
    TEST_ASSERT_EQUAL_STRING("Travel laptop", reader.value().host.hosts[1].name.c_str());
    TEST_ASSERT_TRUE(reader.value().host.activeHost == 9);
    TEST_ASSERT_FALSE(reader.value().host.bluetoothEnabled);
    TEST_ASSERT_EQUAL_UINT8(80, reader.value().soundVolume);
    TEST_ASSERT_FALSE(reader.value().wifi.enabled);
    TEST_ASSERT_EQUAL_STRING("Office WiFi", reader.value().wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("correct horse battery staple",
                             reader.value().wifi.passphrase.c_str());
    TEST_ASSERT_TRUE(reader.value().host.hosts[0].bond == value.host.hosts[0].bond);
    TEST_ASSERT_TRUE(reader.value().host.hosts[0].platform == std::optional<std::string>{"macos"});
    TEST_ASSERT_TRUE(reader.value().host.hosts[0].capabilities ==
                     std::vector<std::string>({"app.activate", "app.active"}));
    TEST_ASSERT_TRUE(reader.value().host.hosts[0].mappingTemplate ==
                     std::optional<std::string>{"macos.default"});
}

void test_wifi_configuration_validation_uses_the_station_contract() {
    // The empty default represents disabled Wi-Fi with no configured network.
    auto value = services::SystemConfiguration{};
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));

    value.wifi = {false, "OpenNetwork", ""};
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));
    value.wifi.enabled = true;
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));

    value.wifi = {true, std::string(32, 'S'), std::string(63, 'p')};
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));
    value.wifi.passphrase = std::string(64, 'a');
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));

    value.wifi = {true, "", ""};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value.wifi = {false, "", "orphaned-secret"};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value.wifi = {false, std::string(33, 'S'), ""};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value.wifi = {false, "Protected", "short"};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value.wifi = {false, "Protected", std::string(64, 'g')};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value.wifi = {false, std::string("Wi\0Fi", 5), ""};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value.wifi = {false, "Protected", std::string("password\0tail", 13)};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
}

void test_version_four_wifi_round_trips_open_protected_enabled_and_disabled_networks() {
    for (const auto& wifi : std::vector<services::WifiConfiguration>{
             {false, "OpenNetwork", ""},
             {true, "Protected", "correct horse battery staple"},
             {false, "RawPsk", std::string(64, 'a')},
         }) {
        MemoryStorage memory;
        core::Storage storage(memory);
        services::ConfigurationService writer(storage), reader(storage);
        auto value = configuration();
        value.wifi = wifi;
        TEST_ASSERT_TRUE(writer.load() == services::ConfigurationResult::Success);
        TEST_ASSERT_TRUE(writer.save(value) == services::ConfigurationResult::Success);
        TEST_ASSERT_EQUAL_UINT8(4, memory.bytes[4]);
        TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::Success);
        TEST_ASSERT_EQUAL(wifi.enabled, reader.value().wifi.enabled);
        TEST_ASSERT_EQUAL_STRING(wifi.ssid.c_str(), reader.value().wifi.ssid.c_str());
        TEST_ASSERT_EQUAL_STRING(wifi.passphrase.c_str(), reader.value().wifi.passphrase.c_str());
    }
}

void test_metadata_validation_is_bounded_and_keeps_capabilities_distinct() {
    auto value = configuration();
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));
    value.host.hosts[0].platform = "a2345678901234567890123456789012";
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));
    value.host.hosts[0].platform = "a23456789012345678901234567890123";
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.host.hosts[0].platform = "Mac OS";
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.host.hosts[0].capabilities = {"app.activate", "app.activate"};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.host.hosts[0].capabilities.assign(
        services::ConfigurationService::maximumHostCapabilityCount + 1, "capability");
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.host.hosts[0].mappingTemplate = "-invalid";
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
}

void test_version_two_migrates_metadata_and_defaults_to_version_four_wifi() {
    MemoryStorage memory;
    memory.bytes = versionTwoConfiguration();
    const auto versionTwo = memory.bytes;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);

    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL(0, memory.writes);
    TEST_ASSERT_TRUE(memory.bytes == versionTwo);
    TEST_ASSERT_EQUAL_UINT8(60, config.value().soundVolume);
    TEST_ASSERT_FALSE(config.value().wifi.enabled);
    TEST_ASSERT_TRUE(config.value().wifi.ssid.empty());
    TEST_ASSERT_TRUE(config.value().wifi.passphrase.empty());
    TEST_ASSERT_TRUE(config.value().host.hosts.front().platform ==
                     std::optional<std::string>{"macos"});
    TEST_ASSERT_TRUE(config.value().host.hosts.front().capabilities ==
                     std::vector<std::string>({"app.activate", "app.active"}));
    TEST_ASSERT_TRUE(config.value().host.hosts.front().mappingTemplate ==
                     std::optional<std::string>{"macos.default"});

    for (std::size_t length = 1; length < versionTwo.size(); ++length) {
        memory.bytes.assign(versionTwo.begin(), versionTwo.begin() + length);
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
        TEST_ASSERT_TRUE(config.value().host.activeHost == 7);
        TEST_ASSERT_EQUAL_UINT8(60, config.value().soundVolume);
    }
    memory.bytes = versionTwo;

    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    auto upgraded = config.value();
    upgraded.soundVolume = 80;
    TEST_ASSERT_TRUE(config.save(upgraded) == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(4, memory.bytes[4]);
    services::ConfigurationService reloaded(storage);
    TEST_ASSERT_TRUE(reloaded.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(80, reloaded.value().soundVolume);
    TEST_ASSERT_TRUE(reloaded.value().host.hosts.front().capabilities ==
                     std::vector<std::string>({"app.activate", "app.active"}));
}

void test_version_three_migrates_losslessly_without_an_eager_write() {
    MemoryStorage memory;
    memory.bytes = versionThreeConfiguration();
    const auto versionThree = memory.bytes;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);

    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL(0, memory.writes);
    TEST_ASSERT_TRUE(memory.bytes == versionThree);
    TEST_ASSERT_EQUAL_UINT8(80, config.value().soundVolume);
    TEST_ASSERT_TRUE(config.value().host.activeHost == 7);
    TEST_ASSERT_TRUE(config.value().host.hosts.front().capabilities ==
                     std::vector<std::string>({"app.activate", "app.active"}));
    TEST_ASSERT_FALSE(config.value().wifi.enabled);
    TEST_ASSERT_TRUE(config.value().wifi.ssid.empty());
    TEST_ASSERT_TRUE(config.value().wifi.passphrase.empty());

    TEST_ASSERT_TRUE(config.save(config.value()) == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(4, memory.bytes[4]);
}

void test_invalid_version_two_lengths_counts_and_duplicates_are_rejected() {
    MemoryStorage memory;
    memory.bytes = versionTwoConfiguration();
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    const auto valid = memory.bytes;

    memory.bytes = valid;
    memory.bytes[6] =
        static_cast<std::uint8_t>(connectivity::BluetoothService::maximumBondCount + 1);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    memory.bytes = valid;
    memory.bytes[49] = static_cast<std::uint8_t>(
        services::ConfigurationService::maximumMetadataIdentifierLength + 1);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    memory.bytes = valid;
    const core::StorageBytes firstCapability(memory.bytes.begin() + 56, memory.bytes.begin() + 69);
    memory.bytes.erase(memory.bytes.begin() + 69, memory.bytes.begin() + 80);
    memory.bytes.insert(memory.bytes.begin() + 69, firstCapability.begin(), firstCapability.end());
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_TRUE(config.value().host.hosts.front().capabilities ==
                     std::vector<std::string>({"app.activate", "app.active"}));
}

void test_failed_version_two_upgrade_preserves_storage_and_published_volume() {
    MemoryStorage memory;
    memory.bytes = versionTwoConfiguration();
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    const auto versionTwo = memory.bytes;
    auto upgraded = config.value();
    upgraded.soundVolume = 80;
    memory.writeError = true;
    TEST_ASSERT_TRUE(config.save(upgraded) == services::ConfigurationResult::StorageError);
    TEST_ASSERT_TRUE(memory.bytes == versionTwo);
    TEST_ASSERT_EQUAL_UINT8(60, config.value().soundVolume);
}

void test_maximum_version_four_record_is_bounded_and_round_trips() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService writer(storage), reader(storage);
    const auto value = maximalConfiguration();
    TEST_ASSERT_TRUE(writer.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(writer.save(value) == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT(services::ConfigurationService::maximumSerializedSize,
                           memory.bytes.size());
    TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(100, reader.value().soundVolume);
    TEST_ASSERT_TRUE(reader.value().wifi.enabled);
    TEST_ASSERT_EQUAL_STRING(value.wifi.ssid.c_str(), reader.value().wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(value.wifi.passphrase.c_str(), reader.value().wifi.passphrase.c_str());
    TEST_ASSERT_TRUE(reader.value().host.hosts.back().capabilities ==
                     value.host.hosts.back().capabilities);
    TEST_ASSERT_TRUE(reader.value().host.hosts.back().platform == value.host.hosts.back().platform);
    TEST_ASSERT_TRUE(reader.value().host.hosts.back().mappingTemplate ==
                     value.host.hosts.back().mappingTemplate);
}

void test_missing_is_off_and_corrupt_or_unknown_records_are_not_overwritten() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_FALSE(config.value().host.bluetoothEnabled);
    TEST_ASSERT_EQUAL_UINT8(60, config.value().soundVolume);
    TEST_ASSERT_FALSE(config.value().wifi.enabled);
    TEST_ASSERT_TRUE(config.value().wifi.ssid.empty());
    TEST_ASSERT_TRUE(config.value().wifi.passphrase.empty());
    TEST_ASSERT_EQUAL(0, memory.writes);
    memory.bytes = {255, 1, 2, 3};
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL(0, memory.writes);
    memory.readError = true;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::StorageError);
}

void test_save_cannot_overwrite_a_configuration_that_never_loaded_successfully() {
    {
        MemoryStorage memory;
        memory.bytes = versionTwoConfiguration();
        const auto existing = memory.bytes;
        core::Storage storage(memory);
        services::ConfigurationService config(storage);
        auto staleDefault = config.value();
        staleDefault.soundVolume = 70;

        TEST_ASSERT_TRUE(config.save(staleDefault) == services::ConfigurationResult::StorageError);
        TEST_ASSERT_EQUAL(0, memory.writes);
        TEST_ASSERT_TRUE(memory.bytes == existing);
        TEST_ASSERT_FALSE(config.loaded());
    }

    MemoryStorage memory;
    memory.bytes = {255, 1, 2, 3};
    const auto corrupt = memory.bytes;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);

    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_TRUE(config.save(configuration()) == services::ConfigurationResult::StorageError);
    TEST_ASSERT_EQUAL(0, memory.writes);
    TEST_ASSERT_TRUE(memory.bytes == corrupt);

    memory.readError = true;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::StorageError);
    TEST_ASSERT_TRUE(config.save(configuration()) == services::ConfigurationResult::StorageError);
    TEST_ASSERT_EQUAL(0, memory.writes);
    TEST_ASSERT_TRUE(memory.bytes == corrupt);
}

void test_invalid_profiles_and_failed_writes_do_not_replace_saved_selection() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    auto value = configuration();
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
    const auto original = memory.bytes;
    value.host.hosts[1].bond = value.host.hosts[0].bond;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_TRUE(memory.bytes == original);
    value = configuration();
    value.host.activeHost = 123;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::InvalidData);
    value = configuration();
    value.host.activeHost = 7;
    memory.writeError = true;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::StorageError);
    TEST_ASSERT_TRUE(config.value().host.activeHost == 9);
    TEST_ASSERT_TRUE(memory.bytes == original);
}
void test_truncated_trailing_and_future_schema_records_preserve_last_valid_value() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(config.save(configuration()) == services::ConfigurationResult::Success);
    const auto valid = memory.bytes;
    for (std::size_t length = 1; length < valid.size(); ++length) {
        memory.bytes.assign(valid.begin(), valid.begin() + length);
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
        TEST_ASSERT_TRUE(config.value().host.activeHost == 9);
    }
    memory.bytes = valid;
    memory.bytes.push_back(0);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    memory.bytes = valid;
    memory.bytes[4] = 6;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL(1, memory.writes);
}

void test_version_five_companion_byte_loads_and_resaves_as_version_four() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService writer(storage), reader(storage);
    auto value = configuration();
    value.wifi = {true, "Protected", "recognizable-secret"};
    TEST_ASSERT_TRUE(writer.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(writer.save(value) == services::ConfigurationResult::Success);
    auto versionFive = memory.bytes;
    versionFive.push_back(0);
    versionFive[4] = 5;
    memory.bytes = versionFive;
    TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reader.value().host.bluetoothEnabled);
    TEST_ASSERT_TRUE(reader.value().host.activeHost == 9);
    TEST_ASSERT_EQUAL_UINT(2, reader.value().host.hosts.size());
    TEST_ASSERT_EQUAL_STRING("Protected", reader.value().wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("recognizable-secret", reader.value().wifi.passphrase.c_str());
    TEST_ASSERT_EQUAL(1, memory.writes);
    TEST_ASSERT_TRUE(reader.save(reader.value()) == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(4, memory.bytes[4]);
    TEST_ASSERT_EQUAL(2, memory.writes);

    versionFive.back() = 1;
    memory.bytes = versionFive;
    TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_STRING("Office laptop", reader.value().host.hosts.front().name.c_str());
    versionFive.back() = 2;
    memory.bytes = versionFive;
    TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_TRUE(reader.value().host.activeHost == 9);
}

void test_invalid_version_four_wifi_payloads_preserve_the_last_valid_value() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    auto value = configuration();
    value.wifi = {true, "Protected", "recognizable-secret"};
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
    const auto valid = memory.bytes;

    memory.bytes = valid;
    const auto wifiEnabledOffset =
        memory.bytes.size() - value.wifi.ssid.size() - value.wifi.passphrase.size() - 3;
    memory.bytes[wifiEnabledOffset] = 2;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL_STRING("Protected", config.value().wifi.ssid.c_str());

    memory.bytes = valid;
    memory.bytes[wifiEnabledOffset + 1] = 33;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL_STRING("recognizable-secret", config.value().wifi.passphrase.c_str());

    memory.bytes = valid;
    const auto passphraseLengthOffset = wifiEnabledOffset + 2 + value.wifi.ssid.size();
    memory.bytes[passphraseLengthOffset] = 65;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL_STRING("recognizable-secret", config.value().wifi.passphrase.c_str());
}

void test_version_one_host_records_migrate_with_default_sound_volume() {
    MemoryStorage memory;
    memory.bytes = versionOneConfiguration();
    const auto versionOne = memory.bytes;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(60, config.value().soundVolume);
    TEST_ASSERT_EQUAL(0, memory.writes);
    TEST_ASSERT_TRUE(config.value().host.activeHost == 7);
    TEST_ASSERT_FALSE(config.value().host.hosts.front().platform.has_value());
    TEST_ASSERT_TRUE(config.value().host.hosts.front().capabilities.empty());
    TEST_ASSERT_FALSE(config.value().host.hosts.front().mappingTemplate.has_value());
    TEST_ASSERT_FALSE(config.value().wifi.enabled);
    TEST_ASSERT_TRUE(config.value().wifi.ssid.empty());
    TEST_ASSERT_TRUE(config.value().wifi.passphrase.empty());
    for (std::size_t length = 1; length < versionOne.size(); ++length) {
        memory.bytes.assign(versionOne.begin(), versionOne.begin() + length);
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
        TEST_ASSERT_TRUE(config.value().host.activeHost == 7);
    }
    memory.bytes = versionOne;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(config.save(config.value()) == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT8(4, memory.bytes[4]);
}

} // namespace

void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_maximum_version_four_record_is_bounded_and_round_trips);
    RUN_TEST(test_version_four_wifi_round_trips_open_protected_enabled_and_disabled_networks);
    RUN_TEST(test_wifi_configuration_validation_uses_the_station_contract);
    RUN_TEST(test_version_two_migrates_metadata_and_defaults_to_version_four_wifi);
    RUN_TEST(test_version_three_migrates_losslessly_without_an_eager_write);
    RUN_TEST(test_invalid_version_two_lengths_counts_and_duplicates_are_rejected);
    RUN_TEST(test_failed_version_two_upgrade_preserves_storage_and_published_volume);
    RUN_TEST(test_metadata_validation_is_bounded_and_keeps_capabilities_distinct);
    RUN_TEST(test_save_cannot_overwrite_a_configuration_that_never_loaded_successfully);
    RUN_TEST(test_truncated_trailing_and_future_schema_records_preserve_last_valid_value);
    RUN_TEST(test_version_five_companion_byte_loads_and_resaves_as_version_four);
    RUN_TEST(test_invalid_version_four_wifi_payloads_preserve_the_last_valid_value);
    RUN_TEST(test_version_one_host_records_migrate_with_default_sound_volume);
    RUN_TEST(test_selection_names_and_off_survive_reload);
    RUN_TEST(test_missing_is_off_and_corrupt_or_unknown_records_are_not_overwritten);
    RUN_TEST(test_invalid_profiles_and_failed_writes_do_not_replace_saved_selection);
    return UNITY_END();
}
