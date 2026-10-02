#include "services/inventory/inventory_service.h"

#include <algorithm>
#include <utility>

namespace cardputer_hub::services {
namespace {

bool readerUsable(NfcServiceState state) {
    switch (state) {
    case NfcServiceState::Idle:
    case NfcServiceState::Reading:
    case NfcServiceState::Ready:
    case NfcServiceState::Writing:
        return true;
    case NfcServiceState::Unavailable:
    case NfcServiceState::Initializing:
    case NfcServiceState::Error:
        break;
    }
    return false;
}

bool failedWrite(NfcWriteState state) {
    return state == NfcWriteState::Failed || state == NfcWriteState::Interrupted ||
           state == NfcWriteState::Refused;
}

} // namespace

InventoryService::InventoryService(NfcService& nfc, RemovableStorageService& storage,
                                   core::IRandomSource& random, core::Logger* logger)
    : nfc_(nfc), storage_(storage), store_(storage.files()), random_(random), logger_(logger) {}

void InventoryService::log(core::LogLevel level, const char* message) const {
    if (logger_ != nullptr)
        logger_->log({level, "inventory", message});
}

void InventoryService::open() {
    if (open_)
        return;
    open_ = true;
    flow_ = Flow::None;
    notice_.reset();
    resetSession(0);
    nfc_.startScanning();
    // Mount the card up front, so the first tap does not wait for it.
    (void)storage_.ensureReady();
    update();
}

void InventoryService::close() {
    if (!open_)
        return;
    open_ = false;
    nfc_.stopScanning();
    flow_ = Flow::None;
    notice_.reset();
    pendingName_.clear();
    attemptId_.reset();
    resetSession(0);
    status_.pendingName.clear();
    status_.hint = InventoryAwaitHint::None;
    status_.screen = InventoryScreen::ReaderUnavailable;
    ++status_.generation;
}

void InventoryService::resetSession(std::uint32_t session) {
    session_ = session;
    lookedUp_ = false;
    status_.session = session;
    status_.id.reset();
    status_.record.reset();
    status_.registered = false;
    ++status_.generation;
}

void InventoryService::show(InventoryScreen screen) {
    if (status_.screen == screen)
        return;
    status_.screen = screen;
    if (screen != InventoryScreen::Known && screen != InventoryScreen::RecordCreated) {
        status_.record.reset();
        status_.registered = false;
    }
    ++status_.generation;
}

void InventoryService::showRecord(const InventoryRecord& record, bool registered) {
    status_.screen = InventoryScreen::Known;
    status_.id = record.id;
    status_.record = record;
    status_.registered = registered;
    ++status_.generation;
}

void InventoryService::showNotice(InventoryScreen screen) {
    notice_ = screen;
    show(screen);
}

void InventoryService::setHint(InventoryAwaitHint hint) {
    if (status_.hint == hint)
        return;
    status_.hint = hint;
    ++status_.generation;
}

void InventoryService::update() {
    if (!open_)
        return;
    if (status_.storageReady != storage_.ready()) {
        status_.storageReady = storage_.ready();
        ++status_.generation;
    }
    const auto& nfc = nfc_.status();
    // A write reports its outcome even when it ended the session.
    if (flow_ == Flow::Writing)
        observeWrite(nfc);
    else if (flow_ == Flow::Erasing)
        observeErase(nfc);
    if (nfc.session != session_) {
        resetSession(nfc.session);
        // A new tag replaces a notice; a tag leaving keeps it on screen.
        if (nfc.session != 0)
            notice_.reset();
    }
    if (!readerUsable(nfc.state)) {
        show(InventoryScreen::ReaderUnavailable);
        return;
    }
    reconcilePendingErase(nfc);
    if (notice_) {
        show(*notice_);
        return;
    }
    if (flow_ == Flow::Awaiting) {
        observeAwaiting(nfc);
        return;
    }
    if (flow_ == Flow::ConfirmingErase || flow_ == Flow::ArmedErase) {
        observeEraseRequest(nfc);
        return;
    }
    if (flow_ != Flow::None)
        return;
    if (session_ == 0) {
        show(InventoryScreen::Waiting);
        return;
    }
    if (lookedUp_)
        return;
    observeContent(nfc);
}

// A registration waits for any blank, writable tag. Other tags are refused
// without being touched; a tag that already failed a write is not retried
// until it is presented again.
void InventoryService::observeAwaiting(const NfcStatus& nfc) {
    if (session_ == 0) {
        if (status_.hint == InventoryAwaitHint::TagNotBlank)
            setHint(InventoryAwaitHint::None);
        show(InventoryScreen::AwaitingTag);
        return;
    }
    if (nfc.tag.content == NfcTagContent::None) {
        show(InventoryScreen::Reading);
        return;
    }
    if (session_ == failedSession_) {
        setHint(InventoryAwaitHint::WriteFailed);
        show(InventoryScreen::AwaitingTag);
        return;
    }
    if (nfc.tag.content == NfcTagContent::Message && attemptId_) {
        const auto tag = parseInventoryTag(nfc.tag.message);
        if (tag.kind == InventoryTagKind::Inventory && tag.id == *attemptId_) {
            // An interrupted attempt left its complete ID, read back here.
            saveRegistered();
            return;
        }
    }
    if (nfc.tag.content != NfcTagContent::Blank || !nfc.tag.writable) {
        setHint(InventoryAwaitHint::TagNotBlank);
        show(InventoryScreen::AwaitingTag);
        return;
    }
    // Registration needs a card: a tag is never written without a place to
    // save its record.
    if (!storage_.ensureReady()) {
        setHint(InventoryAwaitHint::StorageUnavailable);
        show(InventoryScreen::AwaitingTag);
        return;
    }
    attemptId_ = generateInventoryId(random_);
    if (!nfc_.writeMessage(session_, inventoryTagMessage(*attemptId_))) {
        failedSession_ = session_;
        setHint(InventoryAwaitHint::WriteFailed);
        show(InventoryScreen::AwaitingTag);
        return;
    }
    flow_ = Flow::Writing;
    writeSession_ = session_;
    setHint(InventoryAwaitHint::None);
    show(InventoryScreen::Writing);
}

void InventoryService::observeWrite(const NfcStatus& nfc) {
    if (nfc.writeSession != writeSession_)
        return;
    if (nfc.write == NfcWriteState::Verifying) {
        show(InventoryScreen::Verifying);
        return;
    }
    if (nfc.write == NfcWriteState::Succeeded) {
        saveRegistered();
        return;
    }
    if (!failedWrite(nfc.write)) {
        show(InventoryScreen::Writing);
        return;
    }
    // Nothing was saved; the registration keeps waiting for a tag.
    log(core::LogLevel::Warning, "tag registration write failed");
    flow_ = Flow::Awaiting;
    failedSession_ = writeSession_;
    setHint(InventoryAwaitHint::WriteFailed);
    show(InventoryScreen::AwaitingTag);
}

// The tag now carries the verified ID; only then is the record saved.
void InventoryService::saveRegistered() {
    flow_ = Flow::None;
    lookedUp_ = true;
    status_.pendingName.clear();
    setHint(InventoryAwaitHint::None);
    show(InventoryScreen::Saving);
    InventoryRecord record;
    record.id = *attemptId_;
    record.name = pendingName_;
    const auto saved = store_.create(record);
    status_.id = record.id;
    if (saved.status == InventoryStoreStatus::Ok) {
        record.revision = saved.revision;
        attemptId_.reset();
        pendingName_.clear();
        log(core::LogLevel::Info, "tag registered");
        showRecord(record, true);
        return;
    }
    log(core::LogLevel::Warning, "record save failed after tag write");
    showNotice(InventoryScreen::SaveFailed);
}

void InventoryService::observeErase(const NfcStatus& nfc) {
    if (nfc.writeSession != writeSession_)
        return;
    if (!failedWrite(nfc.write) && nfc.write != NfcWriteState::Succeeded) {
        show(InventoryScreen::Erasing);
        return;
    }
    flow_ = Flow::None;
    lookedUp_ = false;
    auto target = std::move(target_);
    target_.reset();
    if (failedWrite(nfc.write)) {
        if (target && target->deleteRecord)
            pendingErases_.push_back(std::move(*target));
        log(core::LogLevel::Warning, "tag erase failed");
        showNotice(InventoryScreen::EraseFailed);
        return;
    }
    if (target)
        completeErase(*target);
}

void InventoryService::reconcilePendingErase(const NfcStatus& nfc) {
    if (pendingErases_.empty() || nfc.state != NfcServiceState::Ready || !nfc.card ||
        nfc.tag.content == NfcTagContent::None || nfc.tag.content == NfcTagContent::ReadFailed)
        return;
    const auto pending =
        std::find_if(pendingErases_.begin(), pendingErases_.end(),
                     [&nfc](const EraseTarget& target) { return target.uid == nfc.card->uid; });
    if (pending == pendingErases_.end())
        return;
    const bool blank = nfc.tag.content == NfcTagContent::Blank;
    auto target = std::move(*pending);
    pendingErases_.erase(pending);
    if (blank)
        completeErase(target);
}

void InventoryService::completeErase(const EraseTarget& target) {
    // The same tag was inspected as blank, or its blank page was read back.
    // Only now can its record be deleted.
    if (target.deleteRecord && target.id) {
        const auto removed = store_.remove(*target.id, std::nullopt);
        if (removed.status != InventoryStoreStatus::Ok &&
            removed.status != InventoryStoreStatus::NotFound) {
            log(core::LogLevel::Warning, "record delete failed after tag erase");
            showNotice(InventoryScreen::RecordNotDeleted);
            return;
        }
    }
    log(core::LogLevel::Info, "tag erased");
    showNotice(InventoryScreen::Erased);
}

void InventoryService::observeContent(const NfcStatus& nfc) {
    switch (nfc.tag.content) {
    case NfcTagContent::None:
        show(InventoryScreen::Reading);
        return;
    case NfcTagContent::Unsupported:
        show(InventoryScreen::Unsupported);
        return;
    case NfcTagContent::NotFormatted:
        show(InventoryScreen::NotFormatted);
        return;
    case NfcTagContent::OtherData:
        show(InventoryScreen::OtherData);
        return;
    case NfcTagContent::ReadFailed:
        show(InventoryScreen::ReadFailed);
        return;
    case NfcTagContent::Blank:
        show(nfc.tag.writable ? InventoryScreen::Blank : InventoryScreen::ReadOnly);
        return;
    case NfcTagContent::Message:
        break;
    }
    const auto tag = parseInventoryTag(nfc.tag.message);
    if (tag.kind != InventoryTagKind::Inventory) {
        show(InventoryScreen::OtherData);
        return;
    }
    lookup(tag.id);
}

void InventoryService::lookup(const InventoryId& id) {
    lookedUp_ = true;
    status_.id = id;
    if (!storage_.ensureReady()) {
        show(InventoryScreen::StorageUnavailable);
        return;
    }
    const auto found = store_.get(id);
    switch (found.status) {
    case InventoryStoreStatus::Ok:
        showRecord(*found.record, false);
        return;
    case InventoryStoreStatus::NotFound:
        show(InventoryScreen::MissingRecord);
        return;
    case InventoryStoreStatus::Invalid:
        show(InventoryScreen::InvalidRecord);
        return;
    default:
        show(InventoryScreen::StorageUnavailable);
        return;
    }
}

InventoryEnrollmentCheck InventoryService::checkEnrollment() {
    if (!storage_.ensureReady())
        return InventoryEnrollmentCheck::StorageUnavailable;
    const auto listed = store_.list();
    if (listed.status == InventoryStoreStatus::TooMany ||
        (listed.status == InventoryStoreStatus::Ok && listed.ids.size() >= inventoryMaxRecords))
        return InventoryEnrollmentCheck::Full;
    if (listed.status != InventoryStoreStatus::Ok)
        return InventoryEnrollmentCheck::StorageUnavailable;
    return InventoryEnrollmentCheck::Ready;
}

std::optional<std::string> InventoryService::normalizeEnteredName(std::string_view name) {
    while (!name.empty() && name.front() == ' ')
        name.remove_prefix(1);
    while (!name.empty() && name.back() == ' ')
        name.remove_suffix(1);
    if (std::any_of(name.begin(), name.end(),
                    [](char character) { return character < 0x20 || character > 0x7E; }) ||
        !isValidInventoryText(name, inventoryMaxNameLength))
        return std::nullopt;
    return std::string(name);
}

InventoryCommandResult InventoryService::startEnrollment(std::string_view name) {
    const auto normalized = normalizeEnteredName(name);
    if (!normalized)
        return InventoryCommandResult::InvalidName;
    if (flow_ != Flow::None)
        return InventoryCommandResult::NotAllowed;
    switch (checkEnrollment()) {
    case InventoryEnrollmentCheck::Ready:
        break;
    case InventoryEnrollmentCheck::StorageUnavailable:
        return InventoryCommandResult::StorageUnavailable;
    case InventoryEnrollmentCheck::Full:
        return InventoryCommandResult::Full;
    }
    flow_ = Flow::Awaiting;
    pendingName_ = *normalized;
    attemptId_.reset();
    failedSession_ = 0;
    notice_.reset();
    status_.pendingName = pendingName_;
    setHint(InventoryAwaitHint::None);
    update();
    return InventoryCommandResult::Started;
}

void InventoryService::cancelEnrollment() {
    if (flow_ != Flow::Awaiting)
        return;
    flow_ = Flow::None;
    pendingName_.clear();
    attemptId_.reset();
    status_.pendingName.clear();
    setHint(InventoryAwaitHint::None);
    lookedUp_ = false;
    update();
}

InventoryCommandResult InventoryService::createRecord(const InventoryId& id,
                                                      std::string_view name) {
    const auto normalized = normalizeEnteredName(name);
    if (!normalized)
        return InventoryCommandResult::InvalidName;
    if (flow_ != Flow::None)
        return InventoryCommandResult::NotAllowed;
    if (!storage_.ensureReady())
        return InventoryCommandResult::StorageUnavailable;
    InventoryRecord record;
    record.id = id;
    record.name = *normalized;
    const auto saved = store_.create(record);
    switch (saved.status) {
    case InventoryStoreStatus::Ok:
        break;
    case InventoryStoreStatus::Exists:
        return InventoryCommandResult::NotAllowed;
    case InventoryStoreStatus::TooMany:
        return InventoryCommandResult::Full;
    case InventoryStoreStatus::Unavailable:
        return InventoryCommandResult::StorageUnavailable;
    default:
        return InventoryCommandResult::Failed;
    }
    record.revision = saved.revision;
    if (session_ != 0 && status_.id && *status_.id == id) {
        // The tag is still on the reader: open its new record.
        notice_.reset();
        lookedUp_ = true;
        showRecord(record, true);
    } else {
        status_.id = id;
        status_.record = record;
        showNotice(InventoryScreen::RecordCreated);
        status_.record = record;
    }
    return InventoryCommandResult::Done;
}

InventoryCommandResult InventoryService::retrySave() {
    if (status_.screen != InventoryScreen::SaveFailed || !attemptId_ || pendingName_.empty())
        return InventoryCommandResult::NotAllowed;
    const auto id = *attemptId_;
    const auto name = pendingName_;
    notice_.reset();
    const auto result = createRecord(id, name);
    if (result == InventoryCommandResult::Done || result == InventoryCommandResult::NotAllowed) {
        attemptId_.reset();
        pendingName_.clear();
        if (result == InventoryCommandResult::NotAllowed) {
            // Saved meanwhile: show what is stored.
            lookedUp_ = false;
            update();
            return InventoryCommandResult::Done;
        }
        return result;
    }
    showNotice(InventoryScreen::SaveFailed);
    return result;
}

bool InventoryService::canErase() const {
    const auto screen = status_.screen;
    if (flow_ != Flow::None || notice_ || session_ == 0)
        return false;
    const auto& nfc = nfc_.status();
    if (!nfc.card || !NfcService::isErasable(nfc.tag))
        return false;
    if (screen == InventoryScreen::OtherData)
        return true;
    if ((screen != InventoryScreen::Known && screen != InventoryScreen::MissingRecord &&
         screen != InventoryScreen::InvalidRecord) ||
        !status_.id || nfc.tag.content != NfcTagContent::Message)
        return false;
    const auto parsed = parseInventoryTag(nfc.tag.message);
    return parsed.kind == InventoryTagKind::Inventory && parsed.id == *status_.id;
}

void InventoryService::setEraseHint(InventoryEraseHint hint) {
    if (status_.eraseHint == hint)
        return;
    status_.eraseHint = hint;
    ++status_.generation;
}

InventoryCommandResult InventoryService::requestErase() {
    if (!canErase())
        return InventoryCommandResult::NotAllowed;
    const auto& nfc = nfc_.status();
    EraseTarget target;
    target.uid = nfc.card->uid;
    target.raw = nfc.tag.raw;
    const bool foreign = status_.screen == InventoryScreen::OtherData;
    if (!foreign) {
        target.id = status_.id;
        // A record known to exist (valid or not) is deleted after the tag.
        target.deleteRecord = status_.screen != InventoryScreen::MissingRecord;
    }
    status_.eraseName = status_.record ? status_.record->name : std::string();
    status_.eraseForeign = foreign;
    status_.eraseDeletesRecord = target.deleteRecord;
    target_ = std::move(target);
    flow_ = Flow::ConfirmingErase;
    setEraseHint(InventoryEraseHint::None);
    show(InventoryScreen::EraseConfirm);
    return InventoryCommandResult::Started;
}

bool InventoryService::targetPresent(const NfcStatus& nfc) const {
    return target_ && session_ != 0 && nfc.state == NfcServiceState::Ready && nfc.card &&
           nfc.card->uid == target_->uid && NfcService::isErasable(nfc.tag) &&
           nfc.tag.raw == target_->raw;
}

InventoryCommandResult InventoryService::confirmErase() {
    if (flow_ != Flow::ConfirmingErase || !target_)
        return InventoryCommandResult::NotAllowed;
    // Never erase an inventory tag while its record cannot be deleted.
    if (target_->deleteRecord && !storage_.ensureReady())
        return InventoryCommandResult::StorageUnavailable;
    const auto& nfc = nfc_.status();
    if (targetPresent(nfc)) {
        startErase();
        return InventoryCommandResult::Started;
    }
    flow_ = Flow::ArmedErase;
    setEraseHint(InventoryEraseHint::None);
    show(InventoryScreen::EraseAwaitingTag);
    update();
    return InventoryCommandResult::Started;
}

void InventoryService::cancelErase() {
    if (flow_ != Flow::ConfirmingErase && flow_ != Flow::ArmedErase)
        return;
    flow_ = Flow::None;
    target_.reset();
    setEraseHint(InventoryEraseHint::None);
    lookedUp_ = false;
    update();
}

void InventoryService::startErase() {
    if (!nfc_.eraseTag(session_, target_->raw)) {
        flow_ = Flow::None;
        target_.reset();
        showNotice(InventoryScreen::EraseFailed);
        return;
    }
    flow_ = Flow::Erasing;
    writeSession_ = session_;
    setEraseHint(InventoryEraseHint::None);
    show(InventoryScreen::Erasing);
}

// The confirmation and the armed erase follow the tag on the reader: only the
// remembered tag, unchanged, is ever erased.
void InventoryService::observeEraseRequest(const NfcStatus& nfc) {
    const bool reading = session_ != 0 && (nfc.state == NfcServiceState::Reading ||
                                           nfc.tag.content == NfcTagContent::None);
    if (flow_ == Flow::ConfirmingErase) {
        if (session_ == 0)
            setEraseHint(InventoryEraseHint::TagRemoved);
        else if (!reading)
            setEraseHint(targetPresent(nfc) ? InventoryEraseHint::None
                                            : InventoryEraseHint::DifferentTag);
        show(InventoryScreen::EraseConfirm);
        return;
    }
    if (session_ == 0 || reading) {
        setEraseHint(InventoryEraseHint::None);
        show(InventoryScreen::EraseAwaitingTag);
        return;
    }
    if (targetPresent(nfc)) {
        startErase();
        return;
    }
    setEraseHint(InventoryEraseHint::DifferentTag);
    show(InventoryScreen::EraseAwaitingTag);
}

void InventoryService::retryStorage() {
    (void)storage_.retryNow();
    if (flow_ == Flow::Awaiting) {
        setHint(InventoryAwaitHint::None);
        update();
        return;
    }
    if (status_.screen == InventoryScreen::StorageUnavailable && status_.id)
        lookup(*status_.id);
}

void InventoryService::acknowledge() {
    if (!notice_)
        return;
    notice_.reset();
    lookedUp_ = false;
    update();
}

InventoryListResult InventoryService::listRecords() {
    if (!storage_.ensureReady())
        return {InventoryStoreStatus::Unavailable, {}};
    return store_.list();
}

InventoryGetResult InventoryService::getRecord(const InventoryId& id) {
    if (!storage_.ensureReady())
        return {InventoryStoreStatus::Unavailable, std::nullopt, {}};
    return store_.get(id);
}

InventoryPutResult InventoryService::putRecord(const InventoryRecord& record,
                                               std::uint32_t expectedRevision) {
    if (!storage_.ensureReady())
        return {InventoryStoreStatus::Unavailable, 0};
    const auto result = store_.put(record, expectedRevision);
    if (result.status == InventoryStoreStatus::Ok && status_.screen == InventoryScreen::Known &&
        status_.id && *status_.id == record.id) {
        // The box on the reader shows its edited contents at once.
        auto shown = record;
        shown.revision = result.revision;
        showRecord(shown, status_.registered);
    }
    return result;
}

InventoryPutResult InventoryService::deleteRecord(const InventoryId& id,
                                                  std::uint32_t expectedRevision) {
    if (!storage_.ensureReady())
        return {InventoryStoreStatus::Unavailable, 0};
    const auto result = store_.remove(id, expectedRevision);
    if (result.status == InventoryStoreStatus::Ok && status_.id && *status_.id == id &&
        flow_ == Flow::None && !notice_ && session_ != 0)
        lookup(id);
    return result;
}

} // namespace cardputer_hub::services
