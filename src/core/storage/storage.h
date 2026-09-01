#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cardputer_hub::core {

using StorageBytes = std::vector<std::uint8_t>;

struct StorageAddress {
    std::string scope;
    std::string key;
};

enum class StorageReadStatus : std::uint8_t {
    Found,
    NotFound,
    InvalidAddress,
    BackendError,
};

struct StorageReadResult {
    StorageReadStatus status;
    StorageBytes data;
};

enum class StorageWriteStatus : std::uint8_t {
    Stored,
    InvalidAddress,
    InvalidData,
    CapacityExceeded,
    BackendError,
};

enum class StorageRemoveStatus : std::uint8_t {
    Removed,
    NotFound,
    InvalidAddress,
    BackendError,
};

class IStorageAdapter {
  public:
    virtual ~IStorageAdapter() = default;
    virtual StorageReadResult read(const StorageAddress& address) = 0;
    virtual StorageWriteStatus write(const StorageAddress& address, const StorageBytes& data) = 0;
    virtual StorageRemoveStatus remove(const StorageAddress& address) = 0;
};

class Storage {
  public:
    explicit Storage(IStorageAdapter& adapter);

    StorageReadResult read(const StorageAddress& address);
    StorageWriteStatus write(const StorageAddress& address, const StorageBytes& data);
    StorageRemoveStatus remove(const StorageAddress& address);

  private:
    IStorageAdapter& adapter_;
};

} // namespace cardputer_hub::core
