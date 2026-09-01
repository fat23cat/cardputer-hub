#include <unity.h>

#include <string>

#include "core/storage/storage.h"

namespace {

using cardputer_hub::core::IStorageAdapter;
using cardputer_hub::core::Storage;
using cardputer_hub::core::StorageAddress;
using cardputer_hub::core::StorageBytes;
using cardputer_hub::core::StorageReadResult;
using cardputer_hub::core::StorageReadStatus;
using cardputer_hub::core::StorageRemoveStatus;
using cardputer_hub::core::StorageWriteStatus;

class RecordingStorageAdapter final : public IStorageAdapter {
  public:
    StorageReadResult read(const StorageAddress& address) override {
        ++readCount;
        lastAddress = address;
        return readResult;
    }

    StorageWriteStatus write(const StorageAddress& address, const StorageBytes& data) override {
        ++writeCount;
        lastAddress = address;
        lastWrittenData = data;
        return writeResult;
    }

    StorageRemoveStatus remove(const StorageAddress& address) override {
        ++removeCount;
        lastAddress = address;
        return removeResult;
    }

    int readCount = 0;
    int writeCount = 0;
    int removeCount = 0;
    StorageAddress lastAddress;
    StorageBytes lastWrittenData;
    StorageReadResult readResult{StorageReadStatus::NotFound, {}};
    StorageWriteStatus writeResult = StorageWriteStatus::Stored;
    StorageRemoveStatus removeResult = StorageRemoveStatus::Removed;
};

void assertInvalidAddress(const StorageAddress& address) {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);

    const auto read = storage.read(address);
    const auto write = storage.write(address, {0x01});
    const auto remove = storage.remove(address);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageReadStatus::InvalidAddress),
                            static_cast<unsigned int>(read.status));
    TEST_ASSERT_TRUE(read.data.empty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageWriteStatus::InvalidAddress),
                            static_cast<unsigned int>(write));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageRemoveStatus::InvalidAddress),
                            static_cast<unsigned int>(remove));
    TEST_ASSERT_EQUAL_INT(0, adapter.readCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.writeCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.removeCount);
}

void test_storage_accepts_one_and_fifteen_byte_address_components() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);
    const StorageAddress oneByte{"a", "b"};
    const StorageAddress fifteenBytes{std::string(15, 's'), std::string(15, 'k')};

    (void)storage.read(oneByte);
    (void)storage.read(fifteenBytes);

    TEST_ASSERT_EQUAL_INT(2, adapter.readCount);
    TEST_ASSERT_EQUAL_STRING(fifteenBytes.scope.c_str(), adapter.lastAddress.scope.c_str());
    TEST_ASSERT_EQUAL_STRING(fifteenBytes.key.c_str(), adapter.lastAddress.key.c_str());
}

void test_storage_rejects_empty_address_components() {
    assertInvalidAddress({"", "key"});
    assertInvalidAddress({"scope", ""});
}

void test_storage_rejects_sixteen_byte_address_components() {
    assertInvalidAddress({std::string(16, 's'), "key"});
    assertInvalidAddress({"scope", std::string(16, 'k')});
}

void test_storage_rejects_embedded_nul_address_components() {
    assertInvalidAddress({std::string{"sc\0pe", 5}, "key"});
    assertInvalidAddress({"scope", std::string{"ke\0y", 4}});
}

void test_storage_forwards_valid_read_address_exactly() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);

    (void)storage.read({"Settings", "ActiveHost"});

    TEST_ASSERT_EQUAL_INT(1, adapter.readCount);
    TEST_ASSERT_EQUAL_STRING("Settings", adapter.lastAddress.scope.c_str());
    TEST_ASSERT_EQUAL_STRING("ActiveHost", adapter.lastAddress.key.c_str());
}

void test_storage_returns_owned_binary_read_data() {
    RecordingStorageAdapter adapter;
    adapter.readResult = {StorageReadStatus::Found, {0x00, 0x7f, 0xff}};
    Storage storage(adapter);

    const auto result = storage.read({"settings", "record"});
    adapter.readResult.data[1] = 0x00;

    const std::uint8_t expected[] = {0x00, 0x7f, 0xff};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageReadStatus::Found),
                            static_cast<unsigned int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), result.data.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, result.data.data(), result.data.size());
}

void test_storage_clears_data_from_unsuccessful_reads() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);

    adapter.readResult = {StorageReadStatus::NotFound, {0xaa}};
    const auto missing = storage.read({"settings", "missing"});
    adapter.readResult = {StorageReadStatus::BackendError, {0xbb}};
    const auto failed = storage.read({"settings", "failed"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageReadStatus::NotFound),
                            static_cast<unsigned int>(missing.status));
    TEST_ASSERT_TRUE(missing.data.empty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageReadStatus::BackendError),
                            static_cast<unsigned int>(failed.status));
    TEST_ASSERT_TRUE(failed.data.empty());
}

void test_storage_forwards_binary_writes_and_replacement_data() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);
    StorageBytes first{0x00, 0x7f, 0xff};

    const auto firstResult = storage.write({"Settings", "Record"}, first);
    first[1] = 0x00;

    const std::uint8_t expectedFirst[] = {0x00, 0x7f, 0xff};
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageWriteStatus::Stored),
                            static_cast<unsigned int>(firstResult));
    TEST_ASSERT_EQUAL_STRING("Settings", adapter.lastAddress.scope.c_str());
    TEST_ASSERT_EQUAL_STRING("Record", adapter.lastAddress.key.c_str());
    TEST_ASSERT_EQUAL_UINT32(sizeof(expectedFirst), adapter.lastWrittenData.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedFirst, adapter.lastWrittenData.data(),
                                  adapter.lastWrittenData.size());

    const StorageBytes replacement{0x42};
    const auto replacementResult = storage.write({"Settings", "Record"}, replacement);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageWriteStatus::Stored),
                            static_cast<unsigned int>(replacementResult));
    TEST_ASSERT_EQUAL_INT(2, adapter.writeCount);
    TEST_ASSERT_EQUAL_UINT32(replacement.size(), adapter.lastWrittenData.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(replacement.data(), adapter.lastWrittenData.data(),
                                  replacement.size());
}

void test_storage_rejects_empty_write_without_calling_adapter() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);

    const auto result = storage.write({"settings", "record"}, {});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageWriteStatus::InvalidData),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(0, adapter.writeCount);
}

void test_storage_propagates_capacity_and_backend_write_failures() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);
    adapter.writeResult = StorageWriteStatus::CapacityExceeded;

    const auto capacity = storage.write({"settings", "record"}, {0x01});
    adapter.writeResult = StorageWriteStatus::BackendError;
    const auto backend = storage.write({"settings", "record"}, {0x02});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageWriteStatus::CapacityExceeded),
                            static_cast<unsigned int>(capacity));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageWriteStatus::BackendError),
                            static_cast<unsigned int>(backend));
}

void test_storage_forwards_valid_removal_address_exactly() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);

    const auto result = storage.remove({"Settings", "ActiveHost"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageRemoveStatus::Removed),
                            static_cast<unsigned int>(result));
    TEST_ASSERT_EQUAL_INT(1, adapter.removeCount);
    TEST_ASSERT_EQUAL_STRING("Settings", adapter.lastAddress.scope.c_str());
    TEST_ASSERT_EQUAL_STRING("ActiveHost", adapter.lastAddress.key.c_str());
}

void test_storage_propagates_missing_and_backend_removal_results() {
    RecordingStorageAdapter adapter;
    Storage storage(adapter);
    adapter.removeResult = StorageRemoveStatus::NotFound;

    const auto missing = storage.remove({"settings", "missing"});
    adapter.removeResult = StorageRemoveStatus::BackendError;
    const auto failed = storage.remove({"settings", "failed"});

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageRemoveStatus::NotFound),
                            static_cast<unsigned int>(missing));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(StorageRemoveStatus::BackendError),
                            static_cast<unsigned int>(failed));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_storage_accepts_one_and_fifteen_byte_address_components);
    RUN_TEST(test_storage_rejects_empty_address_components);
    RUN_TEST(test_storage_rejects_sixteen_byte_address_components);
    RUN_TEST(test_storage_rejects_embedded_nul_address_components);
    RUN_TEST(test_storage_forwards_valid_read_address_exactly);
    RUN_TEST(test_storage_returns_owned_binary_read_data);
    RUN_TEST(test_storage_clears_data_from_unsuccessful_reads);
    RUN_TEST(test_storage_forwards_binary_writes_and_replacement_data);
    RUN_TEST(test_storage_rejects_empty_write_without_calling_adapter);
    RUN_TEST(test_storage_propagates_capacity_and_backend_write_failures);
    RUN_TEST(test_storage_forwards_valid_removal_address_exactly);
    RUN_TEST(test_storage_propagates_missing_and_backend_removal_results);
    return UNITY_END();
}
