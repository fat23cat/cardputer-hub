#include "hardware/storage/microsd/cardputer_microsd_file_storage_adapter.h"

#include <cerrno>
#include <climits>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <SD.h>
#include <SPI.h>

namespace cardputer_hub::hardware {
namespace {

constexpr int microSdClockPin = 40;
constexpr int microSdMisoPin = 39;
constexpr int microSdMosiPin = 14;
constexpr int microSdChipSelectPin = 12;
constexpr std::uint32_t microSdFrequency = 25'000'000;
constexpr const char* mountPoint = "/sd";
constexpr const char* managedRoot = "/cardputer-hub";
constexpr std::size_t maxBackendSegmentLength = 255;

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
        const auto segment = path.substr(segmentStart, segmentEnd - segmentStart);
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.size() > maxBackendSegmentLength) {
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

bool ensureManagedRoot() {
    File root = SD.open(managedRoot, FILE_READ);
    if (root) {
        const bool isDirectory = root.isDirectory();
        root.close();
        return isDirectory;
    }
    return SD.mkdir(managedRoot);
}

bool ensureParentDirectories(const core::FileStoragePath& path) {
    std::string parent(managedRoot);
    std::size_t segmentStart = 0;
    std::size_t separator = path.find('/');
    while (separator != std::string::npos) {
        parent += "/" + path.substr(segmentStart, separator - segmentStart);

        File existing = SD.open(parent.c_str(), FILE_READ);
        if (existing) {
            const bool isDirectory = existing.isDirectory();
            existing.close();
            if (!isDirectory) {
                return false;
            }
        } else if (!SD.mkdir(parent.c_str())) {
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

core::FileStorageState CardputerMicroSdFileStorageAdapter::state() const { return state_; }

core::FileStorageState CardputerMicroSdFileStorageAdapter::refresh() {
    SD.end();
    SPI.begin(microSdClockPin, microSdMisoPin, microSdMosiPin, microSdChipSelectPin);

    if (!SD.begin(microSdChipSelectPin, SPI, microSdFrequency, mountPoint, 5, false)) {
        state_ = SD.cardType() == CARD_NONE ? core::FileStorageState::NotPresent
                                            : core::FileStorageState::MountError;
        return state_;
    }
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        state_ = core::FileStorageState::NotPresent;
        return state_;
    }
    if (!ensureManagedRoot()) {
        SD.end();
        state_ = core::FileStorageState::MountError;
        return state_;
    }

    state_ = core::FileStorageState::Ready;
    return state_;
}

bool CardputerMicroSdFileStorageAdapter::operationBecameUnavailable(int error) {
    if (SD.cardType() != CARD_NONE && !isUnavailableError(error)) {
        return false;
    }

    SD.end();
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

    const std::string backendPath = managedPath(path);
    errno = 0;
    File file = SD.open(backendPath.c_str(), FILE_READ);
    const int openError = errno;
    if (!file) {
        if (operationBecameUnavailable(openError)) {
            return {core::FileReadStatus::Unavailable, {}};
        }
        if (openError == 0 || openError == ENOENT) {
            return {core::FileReadStatus::NotFound, {}};
        }
        return {core::FileReadStatus::BackendError, {}};
    }
    if (file.isDirectory()) {
        file.close();
        return {core::FileReadStatus::BackendError, {}};
    }

    const std::size_t size = file.size();
    if (size > maxSize) {
        file.close();
        return {core::FileReadStatus::TooLarge, {}};
    }

    core::FileStorageBytes data(size);
    if (size == 0) {
        file.close();
        return {core::FileReadStatus::Found, {}};
    }

    errno = 0;
    const std::size_t bytesRead = file.read(data.data(), data.size());
    const int readError = errno;
    file.close();
    if (bytesRead != size) {
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
        const int directoryError = errno;
        if (operationBecameUnavailable(directoryError)) {
            return core::FileWriteStatus::Unavailable;
        }
        return writeFailure(directoryError);
    }

    const std::string backendPath = managedPath(path);
    errno = 0;
    File file = SD.open(backendPath.c_str(), FILE_WRITE);
    const int openError = errno;
    if (!file) {
        if (operationBecameUnavailable(openError)) {
            return core::FileWriteStatus::Unavailable;
        }
        return writeFailure(openError);
    }

    errno = 0;
    const std::size_t bytesWritten = data.empty() ? 0 : file.write(data.data(), data.size());
    const int writeError = errno;
    errno = 0;
    file.flush();
    const int flushError = errno;
    file.close();

    const int operationError = writeError != 0 ? writeError : flushError;
    if (bytesWritten != data.size() || operationError != 0) {
        if (operationBecameUnavailable(operationError)) {
            return core::FileWriteStatus::Unavailable;
        }
        return writeFailure(operationError);
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

    const std::string backendPath = managedPath(path);
    errno = 0;
    File file = SD.open(backendPath.c_str(), FILE_READ);
    const int openError = errno;
    if (!file) {
        if (operationBecameUnavailable(openError)) {
            return core::FileRemoveStatus::Unavailable;
        }
        if (openError == 0 || openError == ENOENT) {
            return core::FileRemoveStatus::NotFound;
        }
        return core::FileRemoveStatus::BackendError;
    }
    if (file.isDirectory()) {
        file.close();
        return core::FileRemoveStatus::BackendError;
    }
    file.close();

    errno = 0;
    if (SD.remove(backendPath.c_str())) {
        return core::FileRemoveStatus::Removed;
    }

    const int removeError = errno;
    if (operationBecameUnavailable(removeError)) {
        return core::FileRemoveStatus::Unavailable;
    }
    if (removeError == ENOENT) {
        return core::FileRemoveStatus::NotFound;
    }
    return core::FileRemoveStatus::BackendError;
}

} // namespace cardputer_hub::hardware
