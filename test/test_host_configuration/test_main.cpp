#include <unity.h>

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
    value.hosts = {{7, "Office laptop", a}, {9, "Travel laptop", b}};
    value.nextHostId = 10;
    value.activeHost = 9;
    value.bluetoothEnabled = true;
    return value;
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
    memory.bytes[4] = 2;
    TEST_ASSERT_TRUE(config.load() == services::ConfigurationResult::InvalidData);
    TEST_ASSERT_EQUAL(1, memory.writes);
}

} // namespace

void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_truncated_trailing_and_future_schema_records_preserve_last_valid_value);
    RUN_TEST(test_selection_names_and_off_survive_reload);
    RUN_TEST(test_missing_is_off_and_corrupt_or_unknown_records_are_not_overwritten);
    RUN_TEST(test_invalid_profiles_and_failed_writes_do_not_replace_saved_selection);
    return UNITY_END();
}
