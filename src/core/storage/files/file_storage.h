#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cardputer_hub::core {

using FileStorageBytes = std::vector<std::uint8_t>;
using FileStoragePath = std::string;

enum class FileStorageState : std::uint8_t {
    Uninitialized,
    Ready,
    NotPresent,
    MountError,
};

enum class FileReadStatus : std::uint8_t {
    Found,
    NotFound,
    InvalidPath,
    InvalidRequest,
    TooLarge,
    Unavailable,
    BackendError,
};

struct FileReadResult {
    FileReadStatus status;
    FileStorageBytes data;
};

enum class FileWriteStatus : std::uint8_t {
    Stored,
    InvalidPath,
    Unavailable,
    ReadOnly,
    CapacityExceeded,
    BackendError,
};

enum class FileRemoveStatus : std::uint8_t {
    Removed,
    NotFound,
    InvalidPath,
    Unavailable,
    BackendError,
};

enum class FileListStatus : std::uint8_t {
    Listed,
    // The directory does not exist; `names` is empty.
    NotFound,
    InvalidPath,
    InvalidRequest,
    // More regular files than the requested bound; `names` is empty.
    TooMany,
    Unavailable,
    BackendError,
};

struct FileListResult {
    FileListStatus status;
    // Regular file names (not paths) in the directory; subdirectories are omitted.
    std::vector<std::string> names;
};

enum class FileRenameStatus : std::uint8_t {
    Renamed,
    NotFound,
    // The destination exists. Rename never replaces a file, as on FAT.
    Exists,
    InvalidPath,
    Unavailable,
    BackendError,
};

class IFileStorageAdapter {
  public:
    virtual ~IFileStorageAdapter() = default;
    [[nodiscard]] virtual FileStorageState state() const = 0;
    virtual FileStorageState refresh() = 0;
    virtual FileReadResult read(const FileStoragePath& path, std::size_t maxSize) = 0;
    virtual FileWriteStatus replace(const FileStoragePath& path, const FileStorageBytes& data) = 0;
    virtual FileRemoveStatus remove(const FileStoragePath& path) = 0;
    virtual FileListResult list(const FileStoragePath& directory, std::size_t maxEntries) = 0;
    virtual FileRenameStatus rename(const FileStoragePath& from, const FileStoragePath& to) = 0;
};

class FileStorage {
  public:
    explicit FileStorage(IFileStorageAdapter& adapter);

    [[nodiscard]] FileStorageState state() const;
    FileStorageState refresh();
    FileReadResult read(const FileStoragePath& path, std::size_t maxSize);
    FileWriteStatus replace(const FileStoragePath& path, const FileStorageBytes& data);
    FileRemoveStatus remove(const FileStoragePath& path);
    FileListResult list(const FileStoragePath& directory, std::size_t maxEntries);
    FileRenameStatus rename(const FileStoragePath& from, const FileStoragePath& to);

    // Replaces `path` so that a power or media failure at any point leaves
    // either the previous complete file or the new complete file readable
    // through readRecoverable(). The new data is written to `<path>.new` and read
    // back; the previous file becomes `<path>.bak`; renaming `.new` to `path` is
    // the commit; the backup is then removed. An interrupted earlier replacement
    // is finished or rolled back first. Nothing else on the card is touched.
    FileWriteStatus replaceRecoverable(const FileStoragePath& path, const FileStorageBytes& data);
    // `path`, or its `.bak` when a replacement stopped between moving the
    // previous file aside and the commit. Never writes.
    FileReadResult readRecoverable(const FileStoragePath& path, std::size_t maxSize);
    // Removes `path` with its temporary and backup files. The file itself goes
    // last, so an interrupted removal leaves the previous file readable rather
    // than resurrecting an older backup. NotFound when neither file nor backup
    // existed.
    FileRemoveStatus removeRecoverable(const FileStoragePath& path);

    static constexpr char recoverableTempSuffix[] = ".new";
    static constexpr char recoverableBackupSuffix[] = ".bak";

  private:
    IFileStorageAdapter& adapter_;
};

} // namespace cardputer_hub::core
