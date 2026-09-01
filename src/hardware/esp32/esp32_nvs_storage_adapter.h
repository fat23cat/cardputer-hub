#pragma once

#include "core/storage/storage.h"

namespace cardputer_hub::hardware {

class Esp32NvsStorageAdapter final : public core::IStorageAdapter {
  public:
    core::StorageReadResult read(const core::StorageAddress& address) override;
    core::StorageWriteStatus write(const core::StorageAddress& address,
                                   const core::StorageBytes& data) override;
    core::StorageRemoveStatus remove(const core::StorageAddress& address) override;

  private:
    bool ensureInitialized();

    bool initialized_ = false;
};

} // namespace cardputer_hub::hardware
