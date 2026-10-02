#pragma once

#include "core/storage/files/file_storage.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>

namespace cardputer_hub::test_support {

// An in-memory card with FAT's relevant behaviour: rename never replaces an
// existing name, and directories exist implicitly. A simulated power or media
// loss lets a set number of mutating operations succeed, tears the next one
// (a replace stores only half of its bytes), and fails everything after it
// until the card is "reinserted".
class MemoryFileStorageAdapter final : public core::IFileStorageAdapter {
  public:
    std::map<std::string, core::FileStorageBytes> files;
    core::FileStorageState currentState = core::FileStorageState::Ready;
    core::FileStorageState refreshedState = core::FileStorageState::Ready;
    std::optional<int> mutationsBeforeLoss;
    std::optional<std::string> readBackendErrorPath;
    int refreshCalls = 0;
    int mutations = 0;

    void reinsert() {
        mutationsBeforeLoss.reset();
        currentState = core::FileStorageState::Ready;
    }

    [[nodiscard]] core::FileStorageState state() const override { return currentState; }

    core::FileStorageState refresh() override {
        ++refreshCalls;
        currentState = refreshedState;
        return currentState;
    }

    core::FileReadResult read(const core::FileStoragePath& path, std::size_t maxSize) override {
        if (currentState != core::FileStorageState::Ready)
            return {core::FileReadStatus::Unavailable, {}};
        if (readBackendErrorPath == path)
            return {core::FileReadStatus::BackendError, {}};
        const auto found = files.find(path);
        if (found == files.end())
            return {core::FileReadStatus::NotFound, {}};
        if (found->second.size() > maxSize)
            return {core::FileReadStatus::TooLarge, {}};
        return {core::FileReadStatus::Found, found->second};
    }

    core::FileWriteStatus replace(const core::FileStoragePath& path,
                                  const core::FileStorageBytes& data) override {
        if (currentState != core::FileStorageState::Ready)
            return core::FileWriteStatus::Unavailable;
        if (lose()) {
            files[path] = core::FileStorageBytes(data.begin(), data.begin() + data.size() / 2);
            return core::FileWriteStatus::Unavailable;
        }
        files[path] = data;
        return core::FileWriteStatus::Stored;
    }

    core::FileRemoveStatus remove(const core::FileStoragePath& path) override {
        if (currentState != core::FileStorageState::Ready)
            return core::FileRemoveStatus::Unavailable;
        if (files.count(path) == 0)
            return core::FileRemoveStatus::NotFound;
        if (lose())
            return core::FileRemoveStatus::Unavailable;
        files.erase(path);
        return core::FileRemoveStatus::Removed;
    }

    core::FileListResult list(const core::FileStoragePath& directory,
                              std::size_t maxEntries) override {
        if (currentState != core::FileStorageState::Ready)
            return {core::FileListStatus::Unavailable, {}};
        const auto prefix = directory + "/";
        core::FileListResult result{core::FileListStatus::NotFound, {}};
        for (const auto& [path, data] : files) {
            if (path.rfind(prefix, 0) != 0)
                continue;
            result.status = core::FileListStatus::Listed;
            const auto name = path.substr(prefix.size());
            if (name.find('/') != std::string::npos)
                continue;
            if (result.names.size() >= maxEntries)
                return {core::FileListStatus::TooMany, {}};
            result.names.push_back(name);
        }
        return result;
    }

    core::FileRenameStatus rename(const core::FileStoragePath& from,
                                  const core::FileStoragePath& to) override {
        if (currentState != core::FileStorageState::Ready)
            return core::FileRenameStatus::Unavailable;
        const auto source = files.find(from);
        if (source == files.end())
            return core::FileRenameStatus::NotFound;
        if (files.count(to) != 0)
            return core::FileRenameStatus::Exists;
        if (lose())
            return core::FileRenameStatus::Unavailable;
        auto data = source->second;
        files.erase(source);
        files[to] = std::move(data);
        return core::FileRenameStatus::Renamed;
    }

    [[nodiscard]] std::optional<std::string> text(const std::string& path) const {
        const auto found = files.find(path);
        if (found == files.end())
            return std::nullopt;
        return std::string(found->second.begin(), found->second.end());
    }

  private:
    // True when this mutation is the one the loss tears.
    bool lose() {
        ++mutations;
        if (!mutationsBeforeLoss)
            return false;
        if (*mutationsBeforeLoss > 0) {
            --*mutationsBeforeLoss;
            return false;
        }
        currentState = core::FileStorageState::NotPresent;
        return true;
    }
};

inline core::FileStorageBytes bytesOf(const std::string& text) {
    return core::FileStorageBytes(text.begin(), text.end());
}

} // namespace cardputer_hub::test_support
