#include "hardware/esp32/esp32_nvs_storage_adapter.h"

#include <cstddef>
#include <utility>

#include <nvs.h>
#include <nvs_flash.h>

namespace cardputer_hub::hardware {
namespace {

constexpr const char* configurationPartition = "hub_config";

class ScopedNvsHandle {
  public:
    explicit ScopedNvsHandle(nvs_handle_t handle) : handle_(handle) {}
    ~ScopedNvsHandle() { nvs_close(handle_); }

    ScopedNvsHandle(const ScopedNvsHandle&) = delete;
    ScopedNvsHandle& operator=(const ScopedNvsHandle&) = delete;

    nvs_handle_t get() const { return handle_; }

  private:
    nvs_handle_t handle_;
};

core::StorageReadResult readFailure(esp_err_t error) {
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return {core::StorageReadStatus::NotFound, {}};
    }
    return {core::StorageReadStatus::BackendError, {}};
}

core::StorageWriteStatus writeFailure(esp_err_t error) {
    if (error == ESP_ERR_NVS_NOT_ENOUGH_SPACE || error == ESP_ERR_NVS_VALUE_TOO_LONG) {
        return core::StorageWriteStatus::CapacityExceeded;
    }
    return core::StorageWriteStatus::BackendError;
}

} // namespace

bool Esp32NvsStorageAdapter::ensureInitialized() {
    if (initialized_) {
        return true;
    }

    initialized_ = nvs_flash_init_partition(configurationPartition) == ESP_OK;
    return initialized_;
}

core::StorageReadResult Esp32NvsStorageAdapter::read(const core::StorageAddress& address) {
    if (!ensureInitialized()) {
        return {core::StorageReadStatus::BackendError, {}};
    }

    nvs_handle_t rawHandle{};
    const esp_err_t openResult = nvs_open_from_partition(
        configurationPartition, address.scope.c_str(), NVS_READONLY, &rawHandle);
    if (openResult != ESP_OK) {
        return readFailure(openResult);
    }
    const ScopedNvsHandle handle(rawHandle);

    std::size_t size = 0;
    const esp_err_t sizeResult = nvs_get_blob(handle.get(), address.key.c_str(), nullptr, &size);
    if (sizeResult != ESP_OK) {
        return readFailure(sizeResult);
    }

    core::StorageBytes data(size);
    if (size == 0) {
        return {core::StorageReadStatus::Found, {}};
    }

    std::size_t loadedSize = data.size();
    const esp_err_t readResult =
        nvs_get_blob(handle.get(), address.key.c_str(), data.data(), &loadedSize);
    if (readResult != ESP_OK) {
        return readFailure(readResult);
    }
    data.resize(loadedSize);
    return {core::StorageReadStatus::Found, std::move(data)};
}

core::StorageWriteStatus Esp32NvsStorageAdapter::write(const core::StorageAddress& address,
                                                       const core::StorageBytes& data) {
    if (!ensureInitialized()) {
        return core::StorageWriteStatus::BackendError;
    }

    nvs_handle_t rawHandle{};
    const esp_err_t openResult = nvs_open_from_partition(
        configurationPartition, address.scope.c_str(), NVS_READWRITE, &rawHandle);
    if (openResult != ESP_OK) {
        return writeFailure(openResult);
    }
    const ScopedNvsHandle handle(rawHandle);

    const esp_err_t writeResult =
        nvs_set_blob(handle.get(), address.key.c_str(), data.data(), data.size());
    if (writeResult != ESP_OK) {
        return writeFailure(writeResult);
    }
    const esp_err_t commitResult = nvs_commit(handle.get());
    if (commitResult != ESP_OK) {
        return writeFailure(commitResult);
    }
    return core::StorageWriteStatus::Stored;
}

core::StorageRemoveStatus Esp32NvsStorageAdapter::remove(const core::StorageAddress& address) {
    if (!ensureInitialized()) {
        return core::StorageRemoveStatus::BackendError;
    }

    nvs_handle_t rawReadHandle{};
    const esp_err_t readOpenResult = nvs_open_from_partition(
        configurationPartition, address.scope.c_str(), NVS_READONLY, &rawReadHandle);
    if (readOpenResult == ESP_ERR_NVS_NOT_FOUND) {
        return core::StorageRemoveStatus::NotFound;
    }
    if (readOpenResult != ESP_OK) {
        return core::StorageRemoveStatus::BackendError;
    }
    {
        const ScopedNvsHandle readHandle(rawReadHandle);
        std::size_t size = 0;
        const esp_err_t findResult =
            nvs_get_blob(readHandle.get(), address.key.c_str(), nullptr, &size);
        if (findResult == ESP_ERR_NVS_NOT_FOUND) {
            return core::StorageRemoveStatus::NotFound;
        }
        if (findResult != ESP_OK) {
            return core::StorageRemoveStatus::BackendError;
        }
    }

    nvs_handle_t rawWriteHandle{};
    if (nvs_open_from_partition(configurationPartition, address.scope.c_str(), NVS_READWRITE,
                                &rawWriteHandle) != ESP_OK) {
        return core::StorageRemoveStatus::BackendError;
    }
    const ScopedNvsHandle writeHandle(rawWriteHandle);
    const esp_err_t removeResult = nvs_erase_key(writeHandle.get(), address.key.c_str());
    if (removeResult == ESP_ERR_NVS_NOT_FOUND) {
        return core::StorageRemoveStatus::NotFound;
    }
    if (removeResult != ESP_OK || nvs_commit(writeHandle.get()) != ESP_OK) {
        return core::StorageRemoveStatus::BackendError;
    }
    return core::StorageRemoveStatus::Removed;
}

} // namespace cardputer_hub::hardware
