#include <unity.h>

#include <cstddef>
#include <string>

#include "../support/memory_file_storage.h"
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

    cardputer_hub::core::FileListResult list(const FileStoragePath& directory,
                                             std::size_t maxEntries) override {
        ++listCalls;
        lastPath = directory;
        lastListLimit = maxEntries;
        return listResult;
    }

    cardputer_hub::core::FileRenameStatus rename(const FileStoragePath& from,
                                                 const FileStoragePath& to) override {
        ++renameCalls;
        lastPath = from;
        lastRenameTarget = to;
        return renameResult;
    }

    FileStorageState currentState = FileStorageState::Uninitialized;
    FileStorageState refreshedState = FileStorageState::Uninitialized;
    FileReadResult readResult{FileReadStatus::Found, {}};
    FileWriteStatus writeResult = FileWriteStatus::Stored;
    FileRemoveStatus removeResult = FileRemoveStatus::Removed;
    cardputer_hub::core::FileListResult listResult{cardputer_hub::core::FileListStatus::Listed, {}};
    cardputer_hub::core::FileRenameStatus renameResult =
        cardputer_hub::core::FileRenameStatus::Renamed;
    int listCalls = 0;
    int renameCalls = 0;
    std::size_t lastListLimit = 0;
    FileStoragePath lastRenameTarget;
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

// ---- Listing and rename -------------------------------------------------------

using cardputer_hub::core::FileListStatus;
using cardputer_hub::core::FileRenameStatus;
using cardputer_hub::test_support::bytesOf;
using cardputer_hub::test_support::MemoryFileStorageAdapter;

void test_listing_is_bounded_and_validated() {
    RecordingFileStorageAdapter adapter;
    FileStorage storage(adapter);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileListStatus::InvalidRequest),
                            static_cast<unsigned>(storage.list("inventory", 0).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileListStatus::InvalidPath),
                            static_cast<unsigned>(storage.list("../x", 4).status));
    TEST_ASSERT_EQUAL_INT(0, adapter.listCalls);

    adapter.listResult = {FileListStatus::Listed, {"a", "b", "c"}};
    const auto overflow = storage.list("inventory", 2);
    // An adapter answer above the bound is never passed on.
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileListStatus::TooMany),
                            static_cast<unsigned>(overflow.status));
    TEST_ASSERT_TRUE(overflow.names.empty());
    const auto listed = storage.list("inventory", 3);
    TEST_ASSERT_EQUAL_UINT(3, listed.names.size());
    TEST_ASSERT_EQUAL_UINT(3, adapter.lastListLimit);
}

void test_rename_never_replaces_and_rejects_unsafe_paths() {
    MemoryFileStorageAdapter adapter;
    FileStorage storage(adapter);
    adapter.files["a.json"] = bytesOf("A");
    adapter.files["b.json"] = bytesOf("B");
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRenameStatus::Exists),
                            static_cast<unsigned>(storage.rename("a.json", "b.json")));
    TEST_ASSERT_EQUAL_STRING("B", adapter.text("b.json")->c_str());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRenameStatus::InvalidPath),
                            static_cast<unsigned>(storage.rename("a.json", "/b")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRenameStatus::InvalidPath),
                            static_cast<unsigned>(storage.rename("a.json", "a.json")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRenameStatus::NotFound),
                            static_cast<unsigned>(storage.rename("c.json", "d.json")));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRenameStatus::Renamed),
                            static_cast<unsigned>(storage.rename("a.json", "c.json")));
    TEST_ASSERT_FALSE(adapter.text("a.json").has_value());
}

// ---- Recoverable replacement --------------------------------------------------

constexpr char recordPath[] = "inventory/records/r.json";

void test_recoverable_replace_commits_and_leaves_no_side_files() {
    MemoryFileStorageAdapter adapter;
    FileStorage storage(adapter);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(FileWriteStatus::Stored),
        static_cast<unsigned>(storage.replaceRecoverable(recordPath, bytesOf("first"))));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(FileWriteStatus::Stored),
        static_cast<unsigned>(storage.replaceRecoverable(recordPath, bytesOf("second"))));
    TEST_ASSERT_EQUAL_UINT(1, adapter.files.size());
    TEST_ASSERT_EQUAL_STRING("second", adapter.text(recordPath)->c_str());
    const auto read = storage.readRecoverable(recordPath, 64);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileReadStatus::Found),
                            static_cast<unsigned>(read.status));
}

void test_recoverable_replace_on_missing_media_writes_nothing() {
    MemoryFileStorageAdapter adapter;
    adapter.currentState = cardputer_hub::core::FileStorageState::NotPresent;
    FileStorage storage(adapter);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(FileWriteStatus::Unavailable),
        static_cast<unsigned>(storage.replaceRecoverable(recordPath, bytesOf("new"))));
    TEST_ASSERT_TRUE(adapter.files.empty());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(FileWriteStatus::InvalidPath),
        static_cast<unsigned>(storage.replaceRecoverable("../x", bytesOf("new"))));
}

// Power is lost after every possible number of completed card operations. On
// the next mount the reader sees the previous file or the new file, complete,
// and the next replacement cleans up and succeeds.
void test_power_loss_at_any_step_preserves_a_complete_record() {
    for (int completed = 0; completed < 12; ++completed) {
        MemoryFileStorageAdapter adapter;
        FileStorage storage(adapter);
        adapter.files[recordPath] = bytesOf("previous-record");
        adapter.mutationsBeforeLoss = completed;
        const auto status = storage.replaceRecoverable(recordPath, bytesOf("replacement-record"));
        adapter.reinsert();

        const auto read = storage.readRecoverable(recordPath, 64);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileReadStatus::Found),
                                static_cast<unsigned>(read.status));
        const std::string text(read.data.begin(), read.data.end());
        if (status == FileWriteStatus::Stored)
            TEST_ASSERT_EQUAL_STRING("replacement-record", text.c_str());
        else
            TEST_ASSERT_TRUE(text == "previous-record" || text == "replacement-record");

        TEST_ASSERT_EQUAL_UINT8(
            static_cast<unsigned>(FileWriteStatus::Stored),
            static_cast<unsigned>(storage.replaceRecoverable(recordPath, bytesOf("next"))));
        TEST_ASSERT_EQUAL_UINT(1, adapter.files.size());
        TEST_ASSERT_EQUAL_STRING("next", adapter.text(recordPath)->c_str());
    }
}

void test_power_loss_while_creating_leaves_nothing_readable() {
    for (int completed = 0; completed < 6; ++completed) {
        MemoryFileStorageAdapter adapter;
        FileStorage storage(adapter);
        adapter.mutationsBeforeLoss = completed;
        const auto status = storage.replaceRecoverable(recordPath, bytesOf("created"));
        adapter.reinsert();
        const auto read = storage.readRecoverable(recordPath, 64);
        if (status == FileWriteStatus::Stored || read.status == FileReadStatus::Found) {
            TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileReadStatus::Found),
                                    static_cast<unsigned>(read.status));
            TEST_ASSERT_EQUAL_STRING("created",
                                     std::string(read.data.begin(), read.data.end()).c_str());
        } else {
            // A torn temporary file is never mistaken for the record.
            TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileReadStatus::NotFound),
                                    static_cast<unsigned>(read.status));
        }
    }
}

void test_recoverable_remove_deletes_the_file_last() {
    MemoryFileStorageAdapter adapter;
    FileStorage storage(adapter);
    const std::string path(recordPath);
    adapter.files[path] = bytesOf("current");
    adapter.files[path + ".bak"] = bytesOf("older");
    adapter.files[path + ".new"] = bytesOf("torn");
    // Power fails after two removals: the file is still the readable record,
    // and the older backup can never reappear.
    adapter.mutationsBeforeLoss = 2;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRemoveStatus::Unavailable),
                            static_cast<unsigned>(storage.removeRecoverable(path)));
    adapter.reinsert();
    const auto survivor = storage.readRecoverable(path, 64);
    TEST_ASSERT_EQUAL_STRING("current",
                             std::string(survivor.data.begin(), survivor.data.end()).c_str());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRemoveStatus::Removed),
                            static_cast<unsigned>(storage.removeRecoverable(path)));
    TEST_ASSERT_TRUE(adapter.files.empty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRemoveStatus::NotFound),
                            static_cast<unsigned>(storage.removeRecoverable(path)));
    // A backup alone is a record too, and is removed as one.
    adapter.files[path + ".bak"] = bytesOf("older");
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FileRemoveStatus::Removed),
                            static_cast<unsigned>(storage.removeRecoverable(path)));
}

void test_backup_is_read_when_the_commit_did_not_happen() {
    MemoryFileStorageAdapter adapter;
    FileStorage storage(adapter);
    adapter.files[std::string(recordPath) + ".bak"] = bytesOf("previous");
    adapter.files[std::string(recordPath) + ".new"] = bytesOf("uncommitted");
    const auto read = storage.readRecoverable(recordPath, 64);
    TEST_ASSERT_EQUAL_STRING("previous", std::string(read.data.begin(), read.data.end()).c_str());
    // Reading never changes the card.
    TEST_ASSERT_EQUAL_UINT(2, adapter.files.size());
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
    RUN_TEST(test_listing_is_bounded_and_validated);
    RUN_TEST(test_rename_never_replaces_and_rejects_unsafe_paths);
    RUN_TEST(test_recoverable_replace_commits_and_leaves_no_side_files);
    RUN_TEST(test_recoverable_replace_on_missing_media_writes_nothing);
    RUN_TEST(test_power_loss_at_any_step_preserves_a_complete_record);
    RUN_TEST(test_power_loss_while_creating_leaves_nothing_readable);
    RUN_TEST(test_recoverable_remove_deletes_the_file_last);
    RUN_TEST(test_backup_is_read_when_the_commit_did_not_happen);
    return UNITY_END();
}
