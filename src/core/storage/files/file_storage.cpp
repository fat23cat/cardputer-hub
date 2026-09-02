#include "core/storage/files/file_storage.h"

#include <string_view>

namespace cardputer_hub::core {
namespace {

bool isValidPath(std::string_view path) {
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
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            return true;
        }
        segmentStart = separator + 1;
    }
    return false;
}

} // namespace

FileStorage::FileStorage(IFileStorageAdapter& adapter) : adapter_(adapter) {}

FileStorageState FileStorage::state() const { return adapter_.state(); }

FileStorageState FileStorage::refresh() { return adapter_.refresh(); }

FileReadResult FileStorage::read(const FileStoragePath& path, std::size_t maxSize) {
    if (!isValidPath(path)) {
        return {FileReadStatus::InvalidPath, {}};
    }
    if (maxSize == 0) {
        return {FileReadStatus::InvalidRequest, {}};
    }

    auto result = adapter_.read(path, maxSize);
    if (result.status != FileReadStatus::Found) {
        result.data.clear();
    }
    return result;
}

FileWriteStatus FileStorage::replace(const FileStoragePath& path, const FileStorageBytes& data) {
    if (!isValidPath(path)) {
        return FileWriteStatus::InvalidPath;
    }
    return adapter_.replace(path, data);
}

FileRemoveStatus FileStorage::remove(const FileStoragePath& path) {
    if (!isValidPath(path)) {
        return FileRemoveStatus::InvalidPath;
    }
    return adapter_.remove(path);
}

} // namespace cardputer_hub::core
