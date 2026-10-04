#include "services/inventory/inventory_store.h"

#include <algorithm>
#include <string_view>

namespace cardputer_hub::services {
namespace {

constexpr std::string_view recordSuffix = ".json";

InventoryStoreStatus fromRead(core::FileReadStatus status) {
    switch (status) {
    case core::FileReadStatus::Found:
        return InventoryStoreStatus::Ok;
    case core::FileReadStatus::NotFound:
        return InventoryStoreStatus::NotFound;
    case core::FileReadStatus::TooLarge:
        return InventoryStoreStatus::Invalid;
    case core::FileReadStatus::Unavailable:
        return InventoryStoreStatus::Unavailable;
    case core::FileReadStatus::InvalidPath:
    case core::FileReadStatus::InvalidRequest:
    case core::FileReadStatus::BackendError:
        break;
    }
    return InventoryStoreStatus::StorageError;
}

// The ID named by a record file, or by the backup a stopped replacement left.
std::optional<InventoryId> idFromName(std::string_view name) {
    const std::string_view backup(core::FileStorage::recoverableBackupSuffix);
    if (name.size() > backup.size() && name.substr(name.size() - backup.size()) == backup)
        name.remove_suffix(backup.size());
    if (name.size() != inventoryIdHexLength + recordSuffix.size() ||
        name.substr(inventoryIdHexLength) != recordSuffix)
        return std::nullopt;
    return parseInventoryIdHex(name.substr(0, inventoryIdHexLength));
}

} // namespace

std::string InventoryStore::pathFor(const InventoryId& id) {
    return std::string(inventoryRecordDirectory) + "/" + inventoryIdHex(id) +
           std::string(recordSuffix);
}

InventoryGetResult InventoryStore::get(const InventoryId& id) {
    InventoryGetResult result;
    const auto read = files_.readRecoverable(pathFor(id), inventoryMaxRecordBytes);
    result.status = fromRead(read.status);
    if (result.status != InventoryStoreStatus::Ok)
        return result;
    const std::string_view text(reinterpret_cast<const char*>(read.data.data()), read.data.size());
    auto decoded = decodeInventoryRecord(text);
    if (!decoded.record || decoded.record->id != id) {
        result.status = InventoryStoreStatus::Invalid;
        return result;
    }
    auto json = encodeInventoryRecord(*decoded.record);
    if (!json) {
        result.status = InventoryStoreStatus::Invalid;
        return result;
    }
    result.record = std::move(decoded.record);
    result.json = std::move(*json);
    return result;
}

InventoryPutResult InventoryStore::write(const InventoryRecord& record) {
    const auto json = encodeInventoryRecord(record);
    if (!json)
        return {InventoryStoreStatus::Rejected, 0};
    const core::FileStorageBytes bytes(json->begin(), json->end());
    switch (files_.replaceRecoverable(pathFor(record.id), bytes)) {
    case core::FileWriteStatus::Stored:
        return {InventoryStoreStatus::Ok, record.revision};
    case core::FileWriteStatus::Unavailable:
        return {InventoryStoreStatus::Unavailable, 0};
    case core::FileWriteStatus::InvalidPath:
    case core::FileWriteStatus::ReadOnly:
    case core::FileWriteStatus::CapacityExceeded:
    case core::FileWriteStatus::BackendError:
        break;
    }
    return {InventoryStoreStatus::StorageError, 0};
}

InventoryPutResult InventoryStore::create(InventoryRecord record) {
    record.revision = 1;
    if (validateInventoryRecord(record) != InventoryRecordError::None)
        return {InventoryStoreStatus::Rejected, 0};
    const auto existing = get(record.id);
    switch (existing.status) {
    case InventoryStoreStatus::NotFound:
        break;
    case InventoryStoreStatus::Ok:
    case InventoryStoreStatus::Invalid:
        return {InventoryStoreStatus::Exists, existing.record ? existing.record->revision : 0};
    default:
        return {existing.status, 0};
    }
    const auto listed = list();
    if (listed.status == InventoryStoreStatus::Ok && listed.ids.size() >= inventoryMaxRecords)
        return {InventoryStoreStatus::TooMany, 0};
    if (listed.status != InventoryStoreStatus::Ok)
        return {listed.status, 0};
    return write(record);
}

InventoryPutResult InventoryStore::put(InventoryRecord record, std::uint32_t expectedRevision) {
    if (expectedRevision == 0 || expectedRevision == UINT32_MAX)
        return {InventoryStoreStatus::Rejected, 0};
    record.revision = expectedRevision + 1;
    if (validateInventoryRecord(record) != InventoryRecordError::None)
        return {InventoryStoreStatus::Rejected, 0};
    const auto current = get(record.id);
    if (current.status != InventoryStoreStatus::Ok)
        return {current.status, 0};
    if (current.record->revision != expectedRevision)
        return {InventoryStoreStatus::Conflict, current.record->revision};
    return write(record);
}

InventoryPutResult InventoryStore::remove(const InventoryId& id,
                                          std::optional<std::uint32_t> expectedRevision) {
    const auto current = get(id);
    if (current.status != InventoryStoreStatus::Ok &&
        current.status != InventoryStoreStatus::Invalid)
        return {current.status, 0};
    const std::uint32_t revision = current.record ? current.record->revision : 0;
    if (expectedRevision && *expectedRevision != revision)
        return {InventoryStoreStatus::Conflict, revision};
    switch (files_.removeRecoverable(pathFor(id))) {
    case core::FileRemoveStatus::Removed:
        return {InventoryStoreStatus::Ok, revision};
    case core::FileRemoveStatus::NotFound:
        return {InventoryStoreStatus::NotFound, 0};
    case core::FileRemoveStatus::Unavailable:
        return {InventoryStoreStatus::Unavailable, 0};
    case core::FileRemoveStatus::InvalidPath:
    case core::FileRemoveStatus::BackendError:
        break;
    }
    return {InventoryStoreStatus::StorageError, 0};
}

InventoryListResult InventoryStore::list() {
    InventoryListResult result;
    // Each record may also have a temporary and a backup file beside it.
    const auto listed = files_.list(inventoryRecordDirectory, inventoryMaxRecords * 3);
    switch (listed.status) {
    case core::FileListStatus::Listed:
        break;
    case core::FileListStatus::NotFound:
        return result;
    case core::FileListStatus::TooMany:
        result.status = InventoryStoreStatus::TooMany;
        return result;
    case core::FileListStatus::Unavailable:
        result.status = InventoryStoreStatus::Unavailable;
        return result;
    case core::FileListStatus::InvalidPath:
    case core::FileListStatus::InvalidRequest:
    case core::FileListStatus::BackendError:
        result.status = InventoryStoreStatus::StorageError;
        return result;
    }
    for (const auto& name : listed.names) {
        const auto id = idFromName(name);
        if (id && std::find(result.ids.begin(), result.ids.end(), *id) == result.ids.end())
            result.ids.push_back(*id);
    }
    std::sort(result.ids.begin(), result.ids.end());
    if (result.ids.size() > inventoryMaxRecords) {
        result.ids.clear();
        result.status = InventoryStoreStatus::TooMany;
    }
    return result;
}

} // namespace cardputer_hub::services
