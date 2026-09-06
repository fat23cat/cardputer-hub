#pragma once

#include "sdmmc_cmd.h"

#include "core/storage/files/file_storage.h"

namespace cardputer_hub::hardware {

class CardputerMicroSdFileStorageAdapter final : public core::IFileStorageAdapter {
  public:
    ~CardputerMicroSdFileStorageAdapter() override;

    [[nodiscard]] core::FileStorageState state() const override;
    core::FileStorageState refresh() override;
    core::FileReadResult read(const core::FileStoragePath& path, std::size_t maxSize) override;
    core::FileWriteStatus replace(const core::FileStoragePath& path,
                                  const core::FileStorageBytes& data) override;
    core::FileRemoveStatus remove(const core::FileStoragePath& path) override;

  private:
    bool operationBecameUnavailable(int error);
    void unmount();

    core::FileStorageState state_ = core::FileStorageState::Uninitialized;
    sdmmc_card_t* card_ = nullptr;
    bool spiBusInitialized_ = false;
};

} // namespace cardputer_hub::hardware
