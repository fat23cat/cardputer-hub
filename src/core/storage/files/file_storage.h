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

class IFileStorageAdapter {
  public:
    virtual ~IFileStorageAdapter() = default;
    [[nodiscard]] virtual FileStorageState state() const = 0;
    virtual FileStorageState refresh() = 0;
    virtual FileReadResult read(const FileStoragePath& path, std::size_t maxSize) = 0;
    virtual FileWriteStatus replace(const FileStoragePath& path, const FileStorageBytes& data) = 0;
    virtual FileRemoveStatus remove(const FileStoragePath& path) = 0;
};

class FileStorage {
  public:
    explicit FileStorage(IFileStorageAdapter& adapter);

    [[nodiscard]] FileStorageState state() const;
    FileStorageState refresh();
    FileReadResult read(const FileStoragePath& path, std::size_t maxSize);
    FileWriteStatus replace(const FileStoragePath& path, const FileStorageBytes& data);
    FileRemoveStatus remove(const FileStoragePath& path);

  private:
    IFileStorageAdapter& adapter_;
};

} // namespace cardputer_hub::core
