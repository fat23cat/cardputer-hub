#pragma once

#include "core/storage/files/file_storage.h"
#include "services/inventory/inventory_record.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cardputer_hub::services {

// Records live below the Cardputer Hub root as inventory/records/<id>.json.
// File names contain only the normalized ID, never user text.
inline constexpr char inventoryRecordDirectory[] = "inventory/records";
inline constexpr std::size_t inventoryMaxRecords = 256;

enum class InventoryStoreStatus : std::uint8_t {
    Ok,
    NotFound,
    // The stored file is not a valid record for its ID. It is left untouched.
    Invalid,
    // The edit was made against an older revision.
    Conflict,
    // A record (valid or not) already exists for this ID.
    Exists,
    // The submitted record is not valid; nothing was written.
    Rejected,
    TooMany,
    Unavailable,
    StorageError,
};

struct InventoryGetResult {
    InventoryStoreStatus status = InventoryStoreStatus::NotFound;
    std::optional<InventoryRecord> record;
    // The canonical JSON of `record`.
    std::string json;
};

struct InventoryPutResult {
    InventoryStoreStatus status = InventoryStoreStatus::StorageError;
    // The committed revision on Ok; the current revision on Conflict.
    std::uint32_t revision = 0;
};

struct InventoryListResult {
    InventoryStoreStatus status = InventoryStoreStatus::Ok;
    // Sorted by ID.
    std::vector<InventoryId> ids;
};

// Validated record files on removable storage. Every write goes through the
// recoverable replacement, so the previous valid record survives until the
// new one is committed. Invalid files are reported, never repaired.
class InventoryStore {
  public:
    explicit InventoryStore(core::FileStorage& files) : files_(files) {}

    [[nodiscard]] InventoryGetResult get(const InventoryId& id);
    // Creates revision 1. Never replaces an existing file.
    [[nodiscard]] InventoryPutResult create(InventoryRecord record);
    // Commits `record` as revision expectedRevision + 1 when the stored record
    // is still at expectedRevision.
    [[nodiscard]] InventoryPutResult put(InventoryRecord record, std::uint32_t expectedRevision);
    [[nodiscard]] InventoryListResult list();
    // Removes a record for good. `expectedRevision` is the revision the
    // deleting editor saw, or 0 for a stored file that is not a valid record;
    // a mismatch is a Conflict with the current revision. Without it (a
    // confirmed erase on the Cardputer) whatever is stored is removed.
    [[nodiscard]] InventoryPutResult remove(const InventoryId& id,
                                            std::optional<std::uint32_t> expectedRevision);

    [[nodiscard]] static std::string pathFor(const InventoryId& id);

  private:
    InventoryPutResult write(const InventoryRecord& record);

    core::FileStorage& files_;
};

} // namespace cardputer_hub::services
