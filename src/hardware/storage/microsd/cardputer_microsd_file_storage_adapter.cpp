#include "hardware/storage/microsd/cardputer_microsd_file_storage_adapter.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace cardputer_hub::hardware {
namespace {

constexpr gpio_num_t microSdClockPin = GPIO_NUM_40;
constexpr gpio_num_t microSdMisoPin = GPIO_NUM_39;
constexpr gpio_num_t microSdMosiPin = GPIO_NUM_14;
constexpr gpio_num_t microSdChipSelectPin = GPIO_NUM_12;
constexpr int microSdFrequencyKhz = 25'000;
constexpr const char* mountPoint = "/sd";
constexpr const char* managedRoot = "/sd/cardputer-hub";
constexpr std::size_t maxBackendSegmentLength = 255;

bool isFatForbiddenCharacter(unsigned char character) {
    constexpr std::string_view forbiddenCharacters = "\"*:<>?|";
    return character < 0x20 || character == 0x7f ||
           forbiddenCharacters.find(static_cast<char>(character)) != std::string_view::npos;
}

bool isValidBackendSegment(std::string_view segment) {
    if (segment.empty() || segment == "." || segment == ".." ||
        segment.size() > maxBackendSegmentLength || segment.back() == '.' ||
        segment.back() == ' ') {
        return false;
    }
    return std::none_of(segment.begin(), segment.end(), [](char character) {
        return isFatForbiddenCharacter(static_cast<unsigned char>(character));
    });
}

bool isSafeLogicalPath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos ||
        path.find('\0') != std::string_view::npos) {
        return false;
    }

    std::size_t segmentStart = 0;
    while (segmentStart < path.size()) {
        const std::size_t separator = path.find('/', segmentStart);
        const std::size_t segmentEnd =
            separator == std::string_view::npos ? path.size() : separator;
        if (!isValidBackendSegment(path.substr(segmentStart, segmentEnd - segmentStart))) {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        segmentStart = separator + 1;
    }
    return std::string_view(managedRoot).size() + 1 + path.size() < PATH_MAX;
}

std::string managedPath(const core::FileStoragePath& path) {
    return std::string(managedRoot) + "/" + path;
}

bool pathIsDirectory(const std::string& path) {
    struct stat status{};
    return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

bool ensureManagedRoot() {
    struct stat status{};
    if (stat(managedRoot, &status) == 0) {
        return S_ISDIR(status.st_mode);
    }
    return mkdir(managedRoot, 0755) == 0;
}

bool ensureParentDirectories(const core::FileStoragePath& path) {
    std::string parent(managedRoot);
    std::size_t segmentStart = 0;
    std::size_t separator = path.find('/');
    while (separator != std::string::npos) {
        parent += "/" + path.substr(segmentStart, separator - segmentStart);
        struct stat status{};
        if (stat(parent.c_str(), &status) == 0) {
            if (!S_ISDIR(status.st_mode)) {
                return false;
            }
        } else if (mkdir(parent.c_str(), 0755) != 0) {
            return false;
        }
        segmentStart = separator + 1;
        separator = path.find('/', segmentStart);
    }
    return true;
}

bool isUnavailableError(int error) { return error == EIO || error == ENODEV || error == ENXIO; }

core::FileWriteStatus writeFailure(int error) {
    if (error == EROFS || error == EACCES || error == EPERM) {
        return core::FileWriteStatus::ReadOnly;
    }
    if (error == ENOSPC) {
        return core::FileWriteStatus::CapacityExceeded;
    }
    return core::FileWriteStatus::BackendError;
}

} // namespace

CardputerMicroSdFileStorageAdapter::~CardputerMicroSdFileStorageAdapter() { unmount(); }

core::FileStorageState CardputerMicroSdFileStorageAdapter::state() const { return state_; }

void CardputerMicroSdFileStorageAdapter::unmount() {
    if (card_ != nullptr) {
        (void)esp_vfs_fat_sdcard_unmount(mountPoint, card_);
        card_ = nullptr;
    }
    if (spiBusInitialized_) {
        const sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        (void)spi_bus_free(static_cast<spi_host_device_t>(host.slot));
        spiBusInitialized_ = false;
    }
}

core::FileStorageState CardputerMicroSdFileStorageAdapter::refresh() {
    unmount();
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = microSdFrequencyKhz;

    spi_bus_config_t busConfig{};
    busConfig.mosi_io_num = microSdMosiPin;
    busConfig.miso_io_num = microSdMisoPin;
    busConfig.sclk_io_num = microSdClockPin;
    busConfig.quadwp_io_num = GPIO_NUM_NC;
    busConfig.quadhd_io_num = GPIO_NUM_NC;
    busConfig.max_transfer_sz = 4096;
    const auto busResult = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &busConfig,
                                              SDSPI_DEFAULT_DMA);
    if (busResult != ESP_OK) {
        state_ = core::FileStorageState::MountError;
        return state_;
    }
    spiBusInitialized_ = true;

    sdspi_device_config_t slotConfig = SDSPI_DEVICE_CONFIG_DEFAULT();
    slotConfig.gpio_cs = microSdChipSelectPin;
    slotConfig.host_id = static_cast<spi_host_device_t>(host.slot);
    esp_vfs_fat_sdmmc_mount_config_t mountConfig{};
    mountConfig.format_if_mount_failed = false;
    mountConfig.max_files = 5;
    mountConfig.allocation_unit_size = 16 * 1024;

    const auto mountResult =
        esp_vfs_fat_sdspi_mount(mountPoint, &host, &slotConfig, &mountConfig, &card_);
    if (mountResult != ESP_OK) {
        unmount();
        state_ = mountResult == ESP_ERR_TIMEOUT || mountResult == ESP_ERR_NOT_FOUND
                     ? core::FileStorageState::NotPresent
                     : core::FileStorageState::MountError;
        return state_;
    }
    if (!ensureManagedRoot()) {
        unmount();
        state_ = core::FileStorageState::MountError;
        return state_;
    }
    state_ = core::FileStorageState::Ready;
    return state_;
}

bool CardputerMicroSdFileStorageAdapter::operationBecameUnavailable(int error) {
    if (!isUnavailableError(error)) {
        return false;
    }
    unmount();
    state_ = error == EIO ? core::FileStorageState::MountError : core::FileStorageState::NotPresent;
    return true;
}

core::FileReadResult CardputerMicroSdFileStorageAdapter::read(const core::FileStoragePath& path,
                                                              std::size_t maxSize) {
    if (!isSafeLogicalPath(path)) {
        return {core::FileReadStatus::InvalidPath, {}};
    }
    if (maxSize == 0) {
        return {core::FileReadStatus::InvalidRequest, {}};
    }
    if (state_ != core::FileStorageState::Ready) {
        return {core::FileReadStatus::Unavailable, {}};
    }

    const auto backendPath = managedPath(path);
    errno = 0;
    std::FILE* file = std::fopen(backendPath.c_str(), "rb");
    const int openError = errno;
    if (file == nullptr) {
        if (operationBecameUnavailable(openError)) {
            return {core::FileReadStatus::Unavailable, {}};
        }
        return {openError == ENOENT ? core::FileReadStatus::NotFound
                                    : core::FileReadStatus::BackendError,
                {}};
    }
    if (pathIsDirectory(backendPath) || std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return {core::FileReadStatus::BackendError, {}};
    }
    const long fileSize = std::ftell(file);
    if (fileSize < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return {core::FileReadStatus::BackendError, {}};
    }
    if (static_cast<std::size_t>(fileSize) > maxSize) {
        std::fclose(file);
        return {core::FileReadStatus::TooLarge, {}};
    }

    core::FileStorageBytes data(static_cast<std::size_t>(fileSize));
    errno = 0;
    const std::size_t bytesRead = data.empty() ? 0 : std::fread(data.data(), 1, data.size(), file);
    const int readError = errno;
    const int closeResult = std::fclose(file);
    if (bytesRead != data.size() || closeResult != 0) {
        if (operationBecameUnavailable(readError)) {
            return {core::FileReadStatus::Unavailable, {}};
        }
        return {core::FileReadStatus::BackendError, {}};
    }
    return {core::FileReadStatus::Found, std::move(data)};
}

core::FileWriteStatus
CardputerMicroSdFileStorageAdapter::replace(const core::FileStoragePath& path,
                                            const core::FileStorageBytes& data) {
    if (!isSafeLogicalPath(path)) {
        return core::FileWriteStatus::InvalidPath;
    }
    if (state_ != core::FileStorageState::Ready) {
        return core::FileWriteStatus::Unavailable;
    }
    errno = 0;
    if (!ensureParentDirectories(path)) {
        const int error = errno;
        return operationBecameUnavailable(error) ? core::FileWriteStatus::Unavailable
                                                 : writeFailure(error);
    }

    const auto backendPath = managedPath(path);
    errno = 0;
    std::FILE* file = std::fopen(backendPath.c_str(), "wb");
    const int openError = errno;
    if (file == nullptr) {
        return operationBecameUnavailable(openError) ? core::FileWriteStatus::Unavailable
                                                     : writeFailure(openError);
    }
    errno = 0;
    const std::size_t bytesWritten =
        data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), file);
    const int writeError = errno;
    errno = 0;
    const int flushResult = std::fflush(file);
    const int flushError = errno;
    const int closeResult = std::fclose(file);
    const int operationError = writeError != 0 ? writeError : flushError;
    if (bytesWritten != data.size() || flushResult != 0 || closeResult != 0) {
        if (operationBecameUnavailable(operationError)) {
            return core::FileWriteStatus::Unavailable;
        }
        return operationError == 0 ? core::FileWriteStatus::CapacityExceeded
                                   : writeFailure(operationError);
    }
    return core::FileWriteStatus::Stored;
}

core::FileRemoveStatus
CardputerMicroSdFileStorageAdapter::remove(const core::FileStoragePath& path) {
    if (!isSafeLogicalPath(path)) {
        return core::FileRemoveStatus::InvalidPath;
    }
    if (state_ != core::FileStorageState::Ready) {
        return core::FileRemoveStatus::Unavailable;
    }
    const auto backendPath = managedPath(path);
    struct stat status{};
    errno = 0;
    if (stat(backendPath.c_str(), &status) != 0) {
        const int error = errno;
        if (operationBecameUnavailable(error)) {
            return core::FileRemoveStatus::Unavailable;
        }
        return error == ENOENT ? core::FileRemoveStatus::NotFound
                               : core::FileRemoveStatus::BackendError;
    }
    if (S_ISDIR(status.st_mode)) {
        return core::FileRemoveStatus::BackendError;
    }
    errno = 0;
    if (std::remove(backendPath.c_str()) == 0) {
        return core::FileRemoveStatus::Removed;
    }
    const int error = errno;
    if (operationBecameUnavailable(error)) {
        return core::FileRemoveStatus::Unavailable;
    }
    return error == ENOENT ? core::FileRemoveStatus::NotFound
                           : core::FileRemoveStatus::BackendError;
}

} // namespace cardputer_hub::hardware
