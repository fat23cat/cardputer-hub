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

services::HostConfiguration configuration() {
    services::HostConfiguration value;
    connectivity::BluetoothBondReference a{}, b{};
    a.bytes[0] = 1;
    b.bytes[0] = 2;
    value.hosts = {{7, "Office laptop", a, std::nullopt, {}, std::nullopt},
                   {9, "Travel laptop", b, std::nullopt, {}, std::nullopt}};
    value.nextHostId = 10;
    value.activeHost = 9;
    value.bluetoothEnabled = true;
    value.hosts[0].platform = "macos";
    value.hosts[0].capabilities = {"app.activate", "app.active"};
    value.hosts[0].mappingTemplate = "macos.default";
    return value;
}

services::HostConfiguration maximalConfiguration() {
    services::HostConfiguration value;
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
        value.hosts.push_back(std::move(host));
    }
    value.nextHostId = static_cast<std::uint32_t>(value.hosts.size()) + 1;
    value.activeHost = value.hosts.back().id;
    value.bluetoothEnabled = true;
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
    const auto reference = [] {
        connectivity::BluetoothBondReference result{};
        result.bytes[0] = 1;
        return result;
    }();
    bytes.insert(bytes.end(), reference.bytes.begin(), reference.bytes.end());
    return bytes;
}

void test_selection_names_and_off_survive_reload() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService writer(storage), reader(storage);
    auto value = configuration();
    value.bluetoothEnabled = false;
    TEST_ASSERT_TRUE(writer.save(value) == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT(2, reader.value().hosts.size());
    TEST_ASSERT_EQUAL_STRING("Travel laptop", reader.value().hosts[1].name.c_str());
    TEST_ASSERT_TRUE(reader.value().activeHost == 9);
    TEST_ASSERT_FALSE(reader.value().bluetoothEnabled);
    TEST_ASSERT_TRUE(reader.value().hosts[0].bond == value.hosts[0].bond);
    TEST_ASSERT_TRUE(reader.value().hosts[0].platform == std::optional<std::string>{"macos"});
    TEST_ASSERT_TRUE(reader.value().hosts[0].capabilities ==
                     std::vector<std::string>({"app.activate", "app.active"}));
    TEST_ASSERT_TRUE(reader.value().hosts[0].mappingTemplate ==
                     std::optional<std::string>{"macos.default"});
}

void test_metadata_validation_is_bounded_and_keeps_host_capabilities_distinct() {
    auto value = configuration();
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));
    value.hosts[0].platform = "a2345678901234567890123456789012";
    TEST_ASSERT_TRUE(services::ConfigurationService::valid(value));
    value.hosts[0].platform = "a23456789012345678901234567890123";
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.hosts[0].platform = "Mac OS";
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.hosts[0].capabilities = {"app.activate", "app.activate"};
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.hosts[0].capabilities.assign(
        services::ConfigurationService::maximumHostCapabilityCount + 1, "capability");
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
    value = configuration();
    value.hosts[0].mappingTemplate = "-invalid";
    TEST_ASSERT_FALSE(services::ConfigurationService::valid(value));
}

void test_version_one_load_is_lazy_and_next_real_save_upgrades_to_version_two() {
    MemoryStorage memory;
    memory.bytes = versionOneConfiguration();
    const auto versionOne = memory.bytes;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL(0, memory.writes);
    TEST_ASSERT_TRUE(memory.bytes == versionOne);
    TEST_ASSERT_EQUAL_UINT(1, config.value().hosts.size());
    TEST_ASSERT_FALSE(config.value().hosts.front().platform.has_value());
    TEST_ASSERT_TRUE(config.value().hosts.front().capabilities.empty());
    TEST_ASSERT_FALSE(config.value().hosts.front().mappingTemplate.has_value());

    for (std::size_t length = 1; length < versionOne.size(); ++length) {
        memory.bytes.assign(versionOne.begin(), versionOne.begin() + length);
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
        TEST_ASSERT_TRUE(config.value().activeHost == 7);
    }
    memory.bytes = versionOne;

    auto upgraded = config.value();
    upgraded.hosts.front().platform = "macos";
    TEST_ASSERT_TRUE(config.save(upgraded) == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL(1, memory.writes);
    TEST_ASSERT_EQUAL_UINT8(2, memory.bytes[4]);
    services::ConfigurationService reloaded(storage);
    TEST_ASSERT_TRUE(reloaded.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_TRUE(reloaded.value().hosts.front().platform ==
                     std::optional<std::string>{"macos"});
}

void test_maximum_version_two_record_is_bounded_and_round_trips_unknown_identifiers() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService writer(storage), reader(storage);
    const auto value = maximalConfiguration();
    TEST_ASSERT_TRUE(writer.save(value) == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT(services::ConfigurationService::maximumSerializedSize,
                           memory.bytes.size());
    TEST_ASSERT_TRUE(reader.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_EQUAL_UINT(connectivity::BluetoothService::maximumBondCount,
                           reader.value().hosts.size());
    TEST_ASSERT_TRUE(reader.value().hosts.back().capabilities == value.hosts.back().capabilities);
    TEST_ASSERT_TRUE(reader.value().hosts.back().platform == value.hosts.back().platform);
    TEST_ASSERT_TRUE(reader.value().hosts.back().mappingTemplate ==
                     value.hosts.back().mappingTemplate);
}

void test_invalid_version_two_lengths_counts_and_duplicates_are_rejected() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    services::HostConfiguration value;
    connectivity::BluetoothBondReference reference{};
    reference.bytes[0] = 1;
    value.hosts = {{1, "A", reference, "p", {"one", "two"}, "t"}};
    value.nextHostId = 2;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
    const auto valid = memory.bytes;

    memory.bytes = valid;
    memory.bytes[6] =
        static_cast<std::uint8_t>(connectivity::BluetoothService::maximumBondCount + 1);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    memory.bytes = valid;
    memory.bytes[37] = static_cast<std::uint8_t>(
        services::ConfigurationService::maximumMetadataIdentifierLength + 1);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    memory.bytes = valid;
    std::copy(memory.bytes.begin() + 41, memory.bytes.begin() + 44, memory.bytes.begin() + 45);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL_UINT(1, config.value().hosts.size());
    TEST_ASSERT_TRUE(config.value().hosts.front().capabilities ==
                     std::vector<std::string>({"one", "two"}));
}

void test_failed_lazy_upgrade_preserves_version_one_storage_and_published_value() {
    MemoryStorage memory;
    memory.bytes = versionOneConfiguration();
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    const auto versionOne = memory.bytes;
    auto upgraded = config.value();
    upgraded.hosts.front().platform = "macos";
    memory.writeError = true;
    TEST_ASSERT_TRUE(config.save(upgraded) == services::ConfigurationResult::StorageError);
    TEST_ASSERT_TRUE(memory.bytes == versionOne);
    TEST_ASSERT_FALSE(config.value().hosts.front().platform.has_value());
}

void test_missing_is_off_and_corrupt_or_unknown_records_are_not_overwritten() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::Success);
    TEST_ASSERT_FALSE(config.value().bluetoothEnabled);
    TEST_ASSERT_EQUAL(0, memory.writes);
    memory.bytes = {255, 1, 2, 3};
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL(0, memory.writes);
    memory.readError = true;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::StorageError);
}

void test_invalid_profiles_and_failed_writes_do_not_replace_saved_selection() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    auto value = configuration();
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::Success);
    const auto original = memory.bytes;
    value.hosts[1].bond = value.hosts[0].bond;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_TRUE(memory.bytes == original);
    value = configuration();
    value.activeHost = 123;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::InvalidData);
    value = configuration();
    value.activeHost = 7;
    memory.writeError = true;
    TEST_ASSERT_TRUE(config.save(value) == services::ConfigurationResult::StorageError);
    TEST_ASSERT_TRUE(config.value().activeHost == 9);
    TEST_ASSERT_TRUE(memory.bytes == original);
}
void test_truncated_trailing_and_future_schema_records_preserve_last_valid_value() {
    MemoryStorage memory;
    core::Storage storage(memory);
    services::ConfigurationService config(storage);
    TEST_ASSERT_TRUE(config.save(configuration()) == services::ConfigurationResult::Success);
    const auto valid = memory.bytes;
    for (std::size_t length = 1; length < valid.size(); ++length) {
        memory.bytes.assign(valid.begin(), valid.begin() + length);
        TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
        TEST_ASSERT_TRUE(config.value().activeHost == 9);
    }
    memory.bytes = valid;
    memory.bytes.push_back(0);
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    memory.bytes = valid;
    memory.bytes[4] = 3;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL(1, memory.writes);
}

} // namespace

void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_invalid_version_two_lengths_counts_and_duplicates_are_rejected);
    RUN_TEST(test_maximum_version_two_record_is_bounded_and_round_trips_unknown_identifiers);
    RUN_TEST(test_failed_lazy_upgrade_preserves_version_one_storage_and_published_value);
    RUN_TEST(test_version_one_load_is_lazy_and_next_real_save_upgrades_to_version_two);
    RUN_TEST(test_metadata_validation_is_bounded_and_keeps_host_capabilities_distinct);
    RUN_TEST(test_truncated_trailing_and_future_schema_records_preserve_last_valid_value);
    RUN_TEST(test_selection_names_and_off_survive_reload);
    RUN_TEST(test_missing_is_off_and_corrupt_or_unknown_records_are_not_overwritten);
    RUN_TEST(test_invalid_profiles_and_failed_writes_do_not_replace_saved_selection);
    return UNITY_END();
}
