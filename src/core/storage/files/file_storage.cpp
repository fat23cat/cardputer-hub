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

FileListResult FileStorage::list(const FileStoragePath& directory, std::size_t maxEntries) {
    if (!isValidPath(directory)) {
        return {FileListStatus::InvalidPath, {}};
    }
    if (maxEntries == 0) {
        return {FileListStatus::InvalidRequest, {}};
    }
    auto result = adapter_.list(directory, maxEntries);
    if (result.status != FileListStatus::Listed || result.names.size() > maxEntries) {
        if (result.status == FileListStatus::Listed) {
            result.status = FileListStatus::TooMany;
        }
        result.names.clear();
    }
    return result;
}

FileRenameStatus FileStorage::rename(const FileStoragePath& from, const FileStoragePath& to) {
    if (!isValidPath(from) || !isValidPath(to) || from == to) {
        return FileRenameStatus::InvalidPath;
    }
    return adapter_.rename(from, to);
}

FileWriteStatus FileStorage::replaceRecoverable(const FileStoragePath& path,
                                                const FileStorageBytes& data) {
    if (!isValidPath(path)) {
        return FileWriteStatus::InvalidPath;
    }
    const auto temp = path + recoverableTempSuffix;
    const auto backup = path + recoverableBackupSuffix;
    const auto unavailableOr = [](bool unavailable) {
        return unavailable ? FileWriteStatus::Unavailable : FileWriteStatus::BackendError;
    };

    // Finish or roll back an interrupted replacement. A backup without the file
    // means the commit never happened: the backup is the previous file. A
    // backup beside the file means the commit happened: the backup is stale.
    switch (adapter_.rename(backup, path)) {
    case FileRenameStatus::Renamed:
    case FileRenameStatus::NotFound:
        break;
    case FileRenameStatus::Exists: {
        const auto removed = adapter_.remove(backup);
        if (removed != FileRemoveStatus::Removed && removed != FileRemoveStatus::NotFound) {
            return unavailableOr(removed == FileRemoveStatus::Unavailable);
        }
        break;
    }
    case FileRenameStatus::InvalidPath:
        return FileWriteStatus::InvalidPath;
    case FileRenameStatus::Unavailable:
        return FileWriteStatus::Unavailable;
    case FileRenameStatus::BackendError:
        return FileWriteStatus::BackendError;
    }
    const auto staleTemp = adapter_.remove(temp);
    if (staleTemp != FileRemoveStatus::Removed && staleTemp != FileRemoveStatus::NotFound) {
        return staleTemp == FileRemoveStatus::InvalidPath
                   ? FileWriteStatus::InvalidPath
                   : unavailableOr(staleTemp == FileRemoveStatus::Unavailable);
    }

    const auto written = adapter_.replace(temp, data);
    if (written != FileWriteStatus::Stored) {
        (void)adapter_.remove(temp);
        return written;
    }
    // Read the new file back before the previous one is moved aside.
    const auto check = adapter_.read(temp, data.size() + 1);
    if (check.status != FileReadStatus::Found || check.data != data) {
        (void)adapter_.remove(temp);
        return unavailableOr(check.status == FileReadStatus::Unavailable);
    }

    const auto aside = adapter_.rename(path, backup);
    if (aside != FileRenameStatus::Renamed && aside != FileRenameStatus::NotFound) {
        (void)adapter_.remove(temp);
        return unavailableOr(aside == FileRenameStatus::Unavailable);
    }
    const auto commit = adapter_.rename(temp, path);
    if (commit != FileRenameStatus::Renamed) {
        if (aside == FileRenameStatus::Renamed) {
            (void)adapter_.rename(backup, path);
        }
        return unavailableOr(commit == FileRenameStatus::Unavailable);
    }
    // A backup left behind here is removed by the next replacement.
    (void)adapter_.remove(backup);
    return FileWriteStatus::Stored;
}

FileReadResult FileStorage::readRecoverable(const FileStoragePath& path, std::size_t maxSize) {
    auto result = read(path, maxSize);
    if (result.status != FileReadStatus::NotFound) {
        return result;
    }
    return read(path + recoverableBackupSuffix, maxSize);
}

FileRemoveStatus FileStorage::removeRecoverable(const FileStoragePath& path) {
    if (!isValidPath(path)) {
        return FileRemoveStatus::InvalidPath;
    }
    bool removedAny = false;
    for (const auto& target :
         {path + recoverableTempSuffix, path + recoverableBackupSuffix, path}) {
        const auto removed = adapter_.remove(target);
        if (removed == FileRemoveStatus::Removed) {
            removedAny = removedAny || target != path + recoverableTempSuffix;
            continue;
        }
        if (removed != FileRemoveStatus::NotFound) {
            return removed;
        }
    }
    return removedAny ? FileRemoveStatus::Removed : FileRemoveStatus::NotFound;
}

} // namespace cardputer_hub::core
