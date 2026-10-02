#pragma once

#include "core/logging/logger.h"
#include "core/platform/random_source.h"
#include "services/inventory/inventory_store.h"
#include "services/nfc/nfc_service.h"
#include "services/storage/removable_storage_service.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cardputer_hub::services {

// What the NFC Mini App shows. Each value is one screen.
enum class InventoryScreen : std::uint8_t {
    ReaderUnavailable,
    Waiting,
    // A tag is present and its data area is being read.
    Reading,
    // Not an NTAG213/215/216.
    Unsupported,
    NotFormatted,
    // Blank but locked: it cannot be registered.
    ReadOnly,
    // Holds other data, which is never overwritten.
    OtherData,
    ReadFailed,
    // Blank and writable: it can be registered.
    Blank,
    Known,
    // A valid inventory ID without a record; a record can be created for it.
    MissingRecord,
    InvalidRecord,
    StorageUnavailable,
    // A registration has its name and waits for any blank, writable tag.
    AwaitingTag,
    Writing,
    Verifying,
    Saving,
    // An erase waits for Enter; it survives the tag leaving.
    EraseConfirm,
    // A confirmed erase waits for the same tag (same UID, same content).
    EraseAwaitingTag,
    Erasing,
    // Notices: shown until acknowledged, or until another tag is presented.
    RecordCreated,
    // The tag carries its new ID but the record could not be saved.
    SaveFailed,
    Erased,
    // Erasure is unconfirmed; the record remains until the same tag is read blank.
    EraseFailed,
    // The tag was erased but its record could not be deleted.
    RecordNotDeleted,
};

// Why AwaitingTag is still waiting.
enum class InventoryAwaitHint : std::uint8_t { None, TagNotBlank, WriteFailed, StorageUnavailable };
// What the erase screens add about the tag on the reader.
enum class InventoryEraseHint : std::uint8_t {
    None,
    TagRemoved,
    CheckingTag,
    TagChanged,
    DifferentTag,
};

struct InventoryStatus {
    InventoryScreen screen = InventoryScreen::ReaderUnavailable;
    std::uint32_t generation = 0;
    // The NFC session this screen belongs to; 0 while no tag is present.
    std::uint32_t session = 0;
    std::optional<InventoryId> id;
    // Known and RecordCreated.
    std::optional<InventoryRecord> record;
    // Known after a registration in this session.
    bool registered = false;
    // The microSD card is mounted. Independent of the reader and the tag.
    bool storageReady = false;
    // AwaitingTag: the name to register and why it still waits.
    std::string pendingName;
    InventoryAwaitHint hint = InventoryAwaitHint::None;
    // Erase screens and the Erased notice: the target's name (empty for a tag
    // with other data), whether it holds other data, and the tag on the reader.
    std::string eraseName;
    bool eraseForeign = false;
    bool eraseDeletesRecord = false;
    InventoryEraseHint eraseHint = InventoryEraseHint::None;
};

enum class InventoryEnrollmentCheck : std::uint8_t { Ready, StorageUnavailable, Full };

enum class InventoryCommandResult : std::uint8_t {
    Started,
    Done,
    InvalidName,
    NotAllowed,
    StorageUnavailable,
    Full,
    Failed,
};

// Maps versioned tag IDs to validated records on microSD and coordinates
// registration and erasing. It reads NfcService state and FileStorage; it never
// draws, and it owns every inventory write, including those from Companion.
class InventoryService {
  public:
    InventoryService(NfcService& nfc, RemovableStorageService& storage, core::IRandomSource& random,
                     core::Logger* logger = nullptr);

    // The NFC Mini App's lifecycle: scanning runs only while it is open.
    void open();
    void close();
    void update();

    [[nodiscard]] const InventoryStatus& status() const noexcept { return status_; }

    // Before name entry: a mounted card with room for one more record.
    [[nodiscard]] InventoryEnrollmentCheck checkEnrollment();
    // Starts a registration with the Cardputer-entered ASCII `name`. No tag is
    // needed yet: the next blank, writable tag presented receives a new random
    // ID, read back, and only then is revision 1 of the record saved. A tag
    // that is not blank is refused and the wait goes on.
    InventoryCommandResult startEnrollment(std::string_view name);
    // Ends a registration that is still waiting for a tag.
    void cancelEnrollment();
    // Creates the record for an existing tag ID. The tag need not be present.
    InventoryCommandResult createRecord(const InventoryId& id, std::string_view name);
    // After SaveFailed: saves the registered tag's record again.
    InventoryCommandResult retrySave();

    // The present tag can be erased: an inventory tag (known, missing or
    // damaged record) or a writable tag holding other NDEF data.
    [[nodiscard]] bool canErase() const;
    // Remembers the present tag (UID and inspected bytes) and asks to confirm.
    InventoryCommandResult requestErase();
    // Erases the remembered tag now if it is on the reader, otherwise as soon
    // as the same tag with the same content returns. The tag is emptied and
    // verified first; only then is an inventory tag's record deleted for good.
    // Nothing is deleted when the tag cannot be erased.
    InventoryCommandResult confirmErase();
    void cancelErase();

    // Mounts the card again and repeats what was waiting for it.
    void retryStorage();
    // Dismisses a notice; the present tag's content is shown again.
    void acknowledge();

    // Companion domain operations. Each mounts the card when needed.
    [[nodiscard]] InventoryListResult listRecords();
    [[nodiscard]] InventoryGetResult getRecord(const InventoryId& id);
    InventoryPutResult putRecord(const InventoryRecord& record, std::uint32_t expectedRevision);
    // `expectedRevision` 0 removes a stored file that is not a valid record.
    InventoryPutResult deleteRecord(const InventoryId& id, std::uint32_t expectedRevision);

    // Printable ASCII, trimmed, 1..inventoryMaxNameLength characters; nullopt
    // otherwise.
    [[nodiscard]] static std::optional<std::string> normalizeEnteredName(std::string_view name);

  private:
    enum class Flow : std::uint8_t {
        None,
        Awaiting,
        Writing,
        ConfirmingErase,
        ArmedErase,
        Erasing
    };

    struct EraseTarget {
        std::vector<std::uint8_t> uid;
        std::vector<std::uint8_t> raw;
        std::optional<InventoryId> id;
        bool deleteRecord = false;
    };

    void show(InventoryScreen screen);
    void showRecord(const InventoryRecord& record, bool registered);
    void showNotice(InventoryScreen screen);
    void setHint(InventoryAwaitHint hint);
    void lookup(const InventoryId& id);
    void observeAwaiting(const NfcStatus& nfc);
    void observeWrite(const NfcStatus& nfc);
    void observeErase(const NfcStatus& nfc);
    void reconcilePendingErase(const NfcStatus& nfc);
    void completeErase(const EraseTarget& target);
    void observeEraseRequest(const NfcStatus& nfc);
    [[nodiscard]] bool targetPresent(const NfcStatus& nfc) const;
    void startErase();
    void setEraseHint(InventoryEraseHint hint);
    void observeContent(const NfcStatus& nfc);
    void saveRegistered();
    void resetSession(std::uint32_t session);
    void log(core::LogLevel level, const char* message) const;

    NfcService& nfc_;
    RemovableStorageService& storage_;
    InventoryStore store_;
    core::IRandomSource& random_;
    core::Logger* logger_;
    InventoryStatus status_;
    bool open_ = false;
    std::uint32_t session_ = 0;
    bool lookedUp_ = false;
    std::optional<InventoryScreen> notice_;
    Flow flow_ = Flow::None;
    std::uint32_t writeSession_ = 0;
    // The ID of the last write attempt: a tag found carrying it was written
    // completely even if its verification was interrupted.
    std::optional<InventoryId> attemptId_;
    // A write to the tag of this session failed: it is not tried again until
    // a tag is presented anew.
    std::uint32_t failedSession_ = 0;
    std::optional<EraseTarget> target_;
    // A failed readback can follow a successful page write. Keep every
    // unresolved target until its UID is inspected again; later erases must
    // not discard earlier targets.
    std::vector<EraseTarget> pendingErases_;
    std::string pendingName_;
};

} // namespace cardputer_hub::services
