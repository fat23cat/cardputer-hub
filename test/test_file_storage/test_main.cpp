#include <unity.h>

#include <cstddef>
#include <string>

#include "core/storage/files/file_storage.h"

namespace {

using cardputer_hub::core::FileReadResult;
using cardputer_hub::core::FileReadStatus;
using cardputer_hub::core::FileRemoveStatus;
using cardputer_hub::core::FileStorage;
using cardputer_hub::core::FileStorageBytes;
using cardputer_hub::core::FileStoragePath;
using cardputer_hub::core::FileStorageState;
using cardputer_hub::core::FileWriteStatus;
using cardputer_hub::core::IFileStorageAdapter;

class RecordingFileStorageAdapter final : public IFileStorageAdapter {
  public:
    FileStorageState state() const override { return currentState; }

    FileStorageState refresh() override {
        ++refreshCalls;
        currentState = refreshedState;
        return currentState;
    }

    FileReadResult read(const FileStoragePath& path, std::size_t maxSize) override {
        ++readCalls;
        lastPath = path;
        lastReadLimit = maxSize;
        return readResult;
    }

    FileWriteStatus replace(const FileStoragePath& path, const FileStorageBytes& data) override {
        ++replaceCalls;
        lastPath = path;
        lastWriteData = data;
        return writeResult;
    }

    FileRemoveStatus remove(const FileStoragePath& path) override {
        ++removeCalls;
        lastPath = path;
        return removeResult;
    }

    FileStorageState currentState = FileStorageState::Uninitialized;
    FileStorageState refreshedState = FileStorageState::Uninitialized;
    FileReadResult readResult{FileReadStatus::Found, {}};
    FileWriteStatus writeResult = FileWriteStatus::Stored;
    FileRemoveStatus removeResult = FileRemoveStatus::Removed;
    int refreshCalls = 0;
    int readCalls = 0;
    int replaceCalls = 0;
    int removeCalls = 0;
    FileStoragePath lastPath;
    std::size_t lastReadLimit = 0;
    FileStorageBytes lastWriteData;
};

void assertInvalidReadPath(FileStorage& storage, const FileStoragePath& path) {
    const auto result = storage.read(path, 16);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileReadStatus::InvalidPath),
                            static_cast<unsigned int>(result.status));
    TEST_ASSERT_TRUE(result.data.empty());
}

void test_paths_accept_relative_segments_and_reject_unsafe_forms() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileReadStatus::Found),
                            static_cast<unsigned int>(storage.read("export.bin", 16).status));
    TEST_ASSERT_EQUAL_STRING("export.bin", adapter.lastPath.c_str());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(FileReadStatus::Found),
        static_cast<unsigned int>(storage.read("backups/hosts/export.bin", 32).status));
    TEST_ASSERT_EQUAL_STRING("backups/hosts/export.bin", adapter.lastPath.c_str());
    TEST_ASSERT_EQUAL_INT(2, adapter.readCalls);

    assertInvalidReadPath(storage, "");
    assertInvalidReadPath(storage, "/absolute.bin");
    assertInvalidReadPath(storage, "folder/");
    assertInvalidReadPath(storage, "folder//file.bin");
    assertInvalidReadPath(storage, "folder/./file.bin");
    assertInvalidReadPath(storage, "folder/../file.bin");
    assertInvalidReadPath(storage, "../outside.bin");
    assertInvalidReadPath(storage, "folder\\file.bin");

    FileStoragePath embeddedNul{"folder"};
    embeddedNul.push_back('\0');
    embeddedNul += "/file.bin";
    assertInvalidReadPath(storage, embeddedNul);
    TEST_ASSERT_EQUAL_INT(2, adapter.readCalls);
}

void test_media_state_and_refresh_results_are_forwarded_exactly() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileStorageState::Uninitialized),
                            static_cast<unsigned int>(storage.state()));

    adapter.currentState = FileStorageState::Ready;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileStorageState::Ready),
                            static_cast<unsigned int>(storage.state()));

    adapter.refreshedState = FileStorageState::NotPresent;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileStorageState::NotPresent),
                            static_cast<unsigned int>(storage.refresh()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileStorageState::NotPresent),
                            static_cast<unsigned int>(storage.state()));

    adapter.refreshedState = FileStorageState::MountError;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileStorageState::MountError),
                            static_cast<unsigned int>(storage.refresh()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileStorageState::MountError),
                            static_cast<unsigned int>(storage.state()));
    TEST_ASSERT_EQUAL_INT(2, adapter.refreshCalls);
}

void test_reads_forward_bounds_and_return_owned_binary_or_empty_files() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);
    adapter.readResult = {FileReadStatus::Found, {0x00, 0x7f, 0xff}};

    auto binary = storage.read("exports/state.bin", 4096);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileReadStatus::Found),
                            static_cast<unsigned int>(binary.status));
    TEST_ASSERT_EQUAL_UINT32(3, binary.data.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, binary.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7f, binary.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xff, binary.data[2]);
    TEST_ASSERT_EQUAL_STRING("exports/state.bin", adapter.lastPath.c_str());
    TEST_ASSERT_EQUAL_UINT32(4096, adapter.lastReadLimit);

    adapter.readResult.data[1] = 0x11;
    TEST_ASSERT_EQUAL_HEX8(0x7f, binary.data[1]);

    adapter.readResult = {FileReadStatus::Found, {}};
    const auto empty = storage.read("exports/empty.bin", 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileReadStatus::Found),
                            static_cast<unsigned int>(empty.status));
    TEST_ASSERT_TRUE(empty.data.empty());
}

void test_unsuccessful_reads_preserve_status_without_exposing_backend_data() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);
    const FileReadStatus failures[] = {FileReadStatus::NotFound, FileReadStatus::TooLarge,
                                       FileReadStatus::Unavailable, FileReadStatus::BackendError};

    for (const auto status : failures) {
        adapter.readResult = {status, {0x10, 0x20}};
        const auto result = storage.read("exports/state.bin", 32);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(status),
                                static_cast<unsigned int>(result.status));
        TEST_ASSERT_TRUE(result.data.empty());
    }
    TEST_ASSERT_EQUAL_INT(4, adapter.readCalls);
}

void test_invalid_read_requests_do_not_reach_the_adapter() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);

    const auto zeroLimit = storage.read("exports/state.bin", 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileReadStatus::InvalidRequest),
                            static_cast<unsigned int>(zeroLimit.status));
    TEST_ASSERT_TRUE(zeroLimit.data.empty());

    const auto invalidPath = storage.read("../outside.bin", 64);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileReadStatus::InvalidPath),
                            static_cast<unsigned int>(invalidPath.status));
    TEST_ASSERT_TRUE(invalidPath.data.empty());
    TEST_ASSERT_EQUAL_INT(0, adapter.readCalls);
}

void test_replacements_forward_owned_binary_empty_and_repeated_contents() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);
    FileStorageBytes contents{0x00, 0x7f, 0xff};

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(FileWriteStatus::Stored),
        static_cast<unsigned int>(storage.replace("exports/state.bin", contents)));
    TEST_ASSERT_EQUAL_STRING("exports/state.bin", adapter.lastPath.c_str());
    TEST_ASSERT_EQUAL_UINT32(3, adapter.lastWriteData.size());
    TEST_ASSERT_EQUAL_HEX8(0x7f, adapter.lastWriteData[1]);

    contents[1] = 0x11;
    TEST_ASSERT_EQUAL_HEX8(0x7f, adapter.lastWriteData[1]);

    const FileStorageBytes replacement{0x42};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned int>(FileWriteStatus::Stored),
        static_cast<unsigned int>(storage.replace("exports/state.bin", replacement)));
    TEST_ASSERT_EQUAL_UINT32(1, adapter.lastWriteData.size());
    TEST_ASSERT_EQUAL_HEX8(0x42, adapter.lastWriteData[0]);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileWriteStatus::Stored),
                            static_cast<unsigned int>(storage.replace("exports/empty.bin", {})));
    TEST_ASSERT_TRUE(adapter.lastWriteData.empty());
    TEST_ASSERT_EQUAL_INT(3, adapter.replaceCalls);
}

void test_replace_outcomes_propagate_without_translation() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);
    const FileWriteStatus outcomes[] = {FileWriteStatus::Unavailable, FileWriteStatus::ReadOnly,
                                        FileWriteStatus::CapacityExceeded,
                                        FileWriteStatus::BackendError};

    for (const auto outcome : outcomes) {
        adapter.writeResult = outcome;
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned int>(outcome),
            static_cast<unsigned int>(storage.replace("exports/state.bin", {0x01})));
    }
    TEST_ASSERT_EQUAL_INT(4, adapter.replaceCalls);
}

void test_invalid_replace_paths_do_not_reach_the_adapter() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileWriteStatus::InvalidPath),
                            static_cast<unsigned int>(storage.replace("", {0x01})));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileWriteStatus::InvalidPath),
                            static_cast<unsigned int>(storage.replace("../outside.bin", {0x01})));
    TEST_ASSERT_EQUAL_INT(0, adapter.replaceCalls);
}

void test_remove_outcomes_propagate_for_present_missing_and_failed_media() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);
    const FileRemoveStatus outcomes[] = {FileRemoveStatus::Removed, FileRemoveStatus::NotFound,
                                         FileRemoveStatus::Unavailable,
                                         FileRemoveStatus::BackendError};

    for (const auto outcome : outcomes) {
        adapter.removeResult = outcome;
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(outcome),
                                static_cast<unsigned int>(storage.remove("exports/state.bin")));
        TEST_ASSERT_EQUAL_STRING("exports/state.bin", adapter.lastPath.c_str());
    }
    TEST_ASSERT_EQUAL_INT(4, adapter.removeCalls);
}

void test_invalid_remove_paths_do_not_reach_the_adapter() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileRemoveStatus::InvalidPath),
                            static_cast<unsigned int>(storage.remove("/outside.bin")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(FileRemoveStatus::InvalidPath),
                            static_cast<unsigned int>(storage.remove("folder//file.bin")));
    TEST_ASSERT_EQUAL_INT(0, adapter.removeCalls);
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_paths_accept_relative_segments_and_reject_unsafe_forms);
    RUN_TEST(test_media_state_and_refresh_results_are_forwarded_exactly);
    RUN_TEST(test_reads_forward_bounds_and_return_owned_binary_or_empty_files);
    RUN_TEST(test_unsuccessful_reads_preserve_status_without_exposing_backend_data);
    RUN_TEST(test_invalid_read_requests_do_not_reach_the_adapter);
    RUN_TEST(test_replacements_forward_owned_binary_empty_and_repeated_contents);
    RUN_TEST(test_replace_outcomes_propagate_without_translation);
    RUN_TEST(test_invalid_replace_paths_do_not_reach_the_adapter);
    RUN_TEST(test_remove_outcomes_propagate_for_present_missing_and_failed_media);
    RUN_TEST(test_invalid_remove_paths_do_not_reach_the_adapter);
    return UNITY_END();
}
