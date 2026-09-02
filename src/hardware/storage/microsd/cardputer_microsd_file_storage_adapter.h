#pragma once

#include "core/storage/files/file_storage.h"

namespace cardputer_hub::hardware {

class CardputerMicroSdFileStorageAdapter final : public core::IFileStorageAdapter {
  public:
    [[nodiscard]] core::FileStorageState state() const override;
    core::FileStorageState refresh() override;
    core::FileReadResult read(const core::FileStoragePath& path, std::size_t maxSize) override;
    core::FileWriteStatus replace(const core::FileStoragePath& path,
                                  const core::FileStorageBytes& data) override;
    core::FileRemoveStatus remove(const core::FileStoragePath& path) override;

  private:
    bool operationBecameUnavailable(int error);

    core::FileStorageState state_ = core::FileStorageState::Uninitialized;
};

} // namespace cardputer_hub::hardware
