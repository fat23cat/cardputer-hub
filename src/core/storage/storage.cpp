#include "core/storage/storage.h"

#include <string_view>

namespace cardputer_hub::core {
namespace {

constexpr std::size_t maxAddressComponentSize = 15;

bool isValidComponent(std::string_view component) {
    return !component.empty() && component.size() <= maxAddressComponentSize &&
           component.find('\0') == std::string_view::npos;
}

bool isValidAddress(const StorageAddress& address) {
    return isValidComponent(address.scope) && isValidComponent(address.key);
}

} // namespace

Storage::Storage(IStorageAdapter& adapter) : adapter_(adapter) {}

StorageReadResult Storage::read(const StorageAddress& address) {
    if (!isValidAddress(address)) {
        return {StorageReadStatus::InvalidAddress, {}};
    }

    auto result = adapter_.read(address);
    if (result.status != StorageReadStatus::Found) {
        result.data.clear();
    }
    return result;
}

StorageWriteStatus Storage::write(const StorageAddress& address, const StorageBytes& data) {
    if (!isValidAddress(address)) {
        return StorageWriteStatus::InvalidAddress;
    }
    if (data.empty()) {
        return StorageWriteStatus::InvalidData;
    }
    return adapter_.write(address, data);
}

StorageRemoveStatus Storage::remove(const StorageAddress& address) {
    if (!isValidAddress(address)) {
        return StorageRemoveStatus::InvalidAddress;
    }
    return adapter_.remove(address);
}

} // namespace cardputer_hub::core
