#include "apps/nfc/nfc_app.h"

namespace cardputer_hub::apps {
using namespace core;
using namespace services;

namespace {

bool isPlain(const InputEvent& event) {
    return !event.modifiers.ctrl && !event.modifiers.alt && !event.modifiers.option;
}

bool isNamed(const InputEvent& event, NamedKey key) {
    return isPlain(event) && !event.modifiers.shift && event.type == InputEventType::NamedKey &&
           event.namedKey == key;
}

bool isEnter(const InputEvent& event) { return isNamed(event, NamedKey::Enter); }

bool isUp(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Up) ||
           (event.type == InputEventType::PrintableCharacter && event.character == ';' &&
            !event.modifiers.shift);
}

bool isDown(const InputEvent& event) {
    return (event.type == InputEventType::NamedKey && event.namedKey == NamedKey::Down) ||
           (event.type == InputEventType::PrintableCharacter && event.character == '.' &&
            !event.modifiers.shift);
}

bool isBusy(InventoryScreen screen) {
    return screen == InventoryScreen::Writing || screen == InventoryScreen::Verifying ||
           screen == InventoryScreen::Saving || screen == InventoryScreen::Erasing;
}

} // namespace

void NfcApp::onActivate() {
    view_.emplace();
    inventory_.open();
}

void NfcApp::onDeactivate() {
    // Closing the app stops the RF scan, so no tag is read or written while
    // another app is in front, and drops the shown contents and any pending
    // registration.
    inventory_.close();
    view_.reset();
}

bool NfcApp::handleBack() {
    if (!view_)
        return false;
    auto& view = *view_;
    if (view.editing) {
        view.editing = false;
        view.draft.clear();
        view.recordFor.reset();
        return true;
    }
    const auto screen = inventory_.status().screen;
    if (screen == InventoryScreen::EraseConfirm || screen == InventoryScreen::EraseAwaitingTag) {
        inventory_.cancelErase();
        return true;
    }
    if (screen == InventoryScreen::AwaitingTag) {
        inventory_.cancelEnrollment();
        return true;
    }
    if (view.detached && view.retainedRecord) {
        view.retainedRecord.reset();
        view.detached = false;
        view.page = 0;
        view.drawnGeneration.reset();
        return true;
    }
    // A tag write in progress is not abandoned by Escape.
    return isBusy(screen);
}

void NfcApp::update(const InputEvents& input, std::chrono::milliseconds) {
    if (!view_)
        return;
    inventory_.update();
    syncWithInventory();
    for (const auto& event : input)
        handle(event);
    syncWithInventory();
    render();
}

// Retain only the rendered record when its tag leaves. InventoryService still
// clears the live tag, so erase and write actions cannot use the snapshot.
void NfcApp::syncWithInventory() {
    auto& view = *view_;
    const auto& status = inventory_.status();
    const bool changed = status.session != view.session || status.screen != view.screen;
    if (status.screen == InventoryScreen::Known && status.record) {
        if (changed)
            view.page = 0;
        view.retainedRecord = *status.record;
        view.retainedRegistered = status.registered;
        view.detached = false;
    } else if (status.screen == InventoryScreen::Waiting ||
               status.screen == InventoryScreen::Reading) {
        view.detached = view.retainedRecord.has_value();
        if (!view.detached && changed)
            view.page = 0;
    } else {
        view.retainedRecord.reset();
        view.detached = false;
        if (changed)
            view.page = 0;
    }
    if (!changed)
        return;
    view.session = status.session;
    view.screen = status.screen;
    view.hint = Hint::None;
}

void NfcApp::handle(const InputEvent& event) {
    if (!isPlain(event))
        return;
    auto& view = *view_;
    if (view.editing) {
        handleEditor(event);
        return;
    }
    const auto& status = inventory_.status();
    if (view.detached && view.retainedRecord) {
        const auto pages = nfcPageCount(nfcDescriptionLines(*view.retainedRecord).size());
        if ((isPageRight(event) || isDown(event)) && view.page + 1 < pages) {
            display_.beginTransition(SlideDirection::Forward);
            ++view.page;
        } else if ((isPageLeft(event) || isUp(event)) && view.page > 0) {
            display_.beginTransition(SlideDirection::Backward);
            --view.page;
        }
        return;
    }
    if (isNamed(event, NamedKey::Delete) && inventory_.canErase()) {
        (void)inventory_.requestErase();
        return;
    }
    switch (status.screen) {
    case InventoryScreen::Known: {
        const auto pages = nfcPageCount(nfcDescriptionLines(*status.record).size());
        if ((isPageRight(event) || isDown(event)) && view.page + 1 < pages) {
            display_.beginTransition(SlideDirection::Forward);
            ++view.page;
        } else if ((isPageLeft(event) || isUp(event)) && view.page > 0) {
            display_.beginTransition(SlideDirection::Backward);
            --view.page;
        }
        return;
    }
    case InventoryScreen::Waiting:
    case InventoryScreen::Blank:
        if (isEnter(event))
            startEditing(std::nullopt);
        return;
    case InventoryScreen::MissingRecord:
        if (isEnter(event) && status.id)
            startEditing(status.id);
        return;
    case InventoryScreen::StorageUnavailable:
        if (isEnter(event))
            inventory_.retryStorage();
        return;
    case InventoryScreen::EraseConfirm:
        if (isEnter(event) &&
            inventory_.confirmErase() == InventoryCommandResult::StorageUnavailable)
            view.hint = Hint::StorageUnavailable;
        return;
    case InventoryScreen::AwaitingTag:
        if (isEnter(event) && status.hint == InventoryAwaitHint::StorageUnavailable)
            inventory_.retryStorage();
        return;
    case InventoryScreen::SaveFailed:
        if (isEnter(event) && inventory_.retrySave() == InventoryCommandResult::StorageUnavailable)
            view.hint = Hint::StorageUnavailable;
        return;
    case InventoryScreen::RecordCreated:
    case InventoryScreen::Erased:
    case InventoryScreen::EraseFailed:
    case InventoryScreen::RecordNotDeleted:
        if (isEnter(event))
            inventory_.acknowledge();
        return;
    default:
        return;
    }
}

void NfcApp::startEditing(std::optional<InventoryId> recordFor) {
    auto& view = *view_;
    switch (inventory_.checkEnrollment()) {
    case InventoryEnrollmentCheck::Ready:
        view.editing = true;
        view.recordFor = recordFor;
        view.draft.clear();
        view.hint = Hint::None;
        return;
    case InventoryEnrollmentCheck::StorageUnavailable:
        view.hint = Hint::StorageUnavailable;
        return;
    case InventoryEnrollmentCheck::Full:
        view.hint = Hint::Full;
        return;
    }
}

void NfcApp::handleEditor(const InputEvent& event) {
    auto& view = *view_;
    if (event.type == InputEventType::NamedKey) {
        if (event.namedKey == NamedKey::Backspace && !view.draft.empty())
            view.draft.pop_back();
        else if (isEnter(event) && !view.draft.empty())
            submitName();
        return;
    }
    if (event.type != InputEventType::PrintableCharacter)
        return;
    // The Cardputer keyboard enters ASCII; Cyrillic text is edited on the Mac.
    if (event.character < 0x20 || event.character > 0x7E ||
        view.draft.size() >= inventoryMaxNameLength ||
        (view.draft.empty() && event.character == ' '))
        return;
    view.draft.push_back(event.character);
}

void NfcApp::submitName() {
    auto& view = *view_;
    const auto result = view.recordFor ? inventory_.createRecord(*view.recordFor, view.draft)
                                       : inventory_.startEnrollment(view.draft);
    switch (result) {
    case InventoryCommandResult::InvalidName:
        return;
    case InventoryCommandResult::StorageUnavailable:
        view.hint = Hint::StorageUnavailable;
        break;
    case InventoryCommandResult::Full:
        view.hint = Hint::Full;
        break;
    default:
        break;
    }
    view.editing = false;
    view.draft.clear();
    view.recordFor.reset();
}

NfcMessage NfcApp::message(const InventoryStatus& status) const {
    const auto hint = view_->hint;
    NfcMessage message;
    switch (status.screen) {
    case InventoryScreen::ReaderUnavailable:
        message = {"NFC READER UNAVAILABLE", {"CHECK UNIT NFC ON GROVE"}, "", "", true};
        break;
    case InventoryScreen::Waiting:
        message = {"TAP A TAG", {")))"}, "", "ENTER  NEW"};
        break;
    case InventoryScreen::Reading:
        message = {"READING TAG", {"HOLD IT STILL"}, "", ""};
        break;
    case InventoryScreen::Unsupported:
        message = {"UNSUPPORTED TAG", {"USE AN NTAG213/215/216", "STICKER"}, "", ""};
        break;
    case InventoryScreen::NotFormatted:
        message = {"TAG NOT NDEF FORMATTED", {"IT CANNOT BE REGISTERED"}, "", ""};
        break;
    case InventoryScreen::ReadOnly:
        message = {"TAG IS LOCKED", {"IT CANNOT BE REGISTERED"}, "", ""};
        break;
    case InventoryScreen::OtherData:
        message = {"TAG HOLDS OTHER DATA",
                   {"IT IS NOT OVERWRITTEN"},
                   inventory_.canErase() ? "FN+DEL  ERASE" : "",
                   ""};
        break;
    case InventoryScreen::ReadFailed:
        message = {"COULD NOT READ TAG", {"TAP IT AGAIN"}, "", "", true};
        break;
    case InventoryScreen::Blank:
        message = {"BLANK TAG", {"REGISTER IT FOR A BOX OR BAG"}, "", "ENTER  REGISTER"};
        break;
    case InventoryScreen::Known:
        break;
    case InventoryScreen::MissingRecord:
        message = {"NO RECORD FOR THIS TAG",
                   {"ITS ID IS KEPT"},
                   inventory_.canErase() ? "FN+DEL  ERASE" : "",
                   "ENTER  CREATE"};
        break;
    case InventoryScreen::InvalidRecord:
        message = {"RECORD IS DAMAGED",
                   {"IT IS LEFT UNCHANGED"},
                   inventory_.canErase() ? "FN+DEL  ERASE" : "",
                   "",
                   true};
        break;
    case InventoryScreen::StorageUnavailable:
        message = {"MICROSD UNAVAILABLE", {"INSERT THE CARD"}, "", "ENTER  RETRY", true};
        break;
    case InventoryScreen::AwaitingTag: {
        message = {"TAP TAG TO WRITE", {status.pendingName}, "ESC  CANCEL", ""};
        switch (status.hint) {
        case InventoryAwaitHint::None:
            message.lines.emplace_back("ANY BLANK NTAG STICKER");
            break;
        case InventoryAwaitHint::TagNotBlank:
            message.lines.emplace_back("THIS TAG IS NOT BLANK");
            break;
        case InventoryAwaitHint::WriteFailed:
            message.lines.emplace_back("TAG NOT WRITTEN, TAP AGAIN");
            break;
        case InventoryAwaitHint::StorageUnavailable:
            message.lines.emplace_back("INSERT MICROSD");
            message.footerRight = "ENTER  RETRY";
            break;
        }
        break;
    }
    case InventoryScreen::Writing:
        message = {"WRITING TAG", {"KEEP THE TAG ON THE READER"}, "", ""};
        break;
    case InventoryScreen::Verifying:
        message = {"CHECKING TAG", {"KEEP THE TAG ON THE READER"}, "", ""};
        break;
    case InventoryScreen::Saving:
        message = {"SAVING RECORD", {}, "", ""};
        break;
    case InventoryScreen::EraseConfirm: {
        message = {"ERASE THIS TAG?", {}, "ESC  CANCEL", "ENTER  ERASE", true};
        if (status.eraseForeign) {
            message.lines = {"ITS OTHER DATA IS LOST", "IT CANNOT BE RESTORED"};
        } else {
            if (!status.eraseName.empty())
                message.lines.push_back(status.eraseName);
            message.lines.emplace_back(status.eraseDeletesRecord ? "ITS RECORD IS DELETED"
                                                                 : "THE TAG BECOMES BLANK");
        }
        if (status.eraseHint == InventoryEraseHint::TagRemoved)
            message.lines.emplace_back("TAG REMOVED; ENTER, THEN TAP IT");
        else if (status.eraseHint == InventoryEraseHint::CheckingTag) {
            message.lines.emplace_back("CHECKING TAG; WAIT");
            message.footerRight.clear();
        } else if (status.eraseHint == InventoryEraseHint::TagChanged) {
            message.lines.emplace_back("TAG DATA CHANGED");
            message.footerRight.clear();
        } else if (status.eraseHint == InventoryEraseHint::DifferentTag) {
            message.lines.emplace_back("THIS IS A DIFFERENT TAG");
            message.footerRight.clear();
        }
        break;
    }
    case InventoryScreen::EraseAwaitingTag: {
        const auto detail =
            status.eraseHint == InventoryEraseHint::DifferentTag  ? "THIS IS A DIFFERENT TAG"
            : status.eraseHint == InventoryEraseHint::TagChanged  ? "TAG DATA CHANGED"
            : status.eraseHint == InventoryEraseHint::CheckingTag ? "CHECKING TAG"
                                                                  : "THE SAME TAG, UNCHANGED";
        message = {"PUT THE TAG BACK",
                   {status.eraseForeign ? "TO ERASE ITS OTHER DATA" : status.eraseName, detail},
                   "ESC  CANCEL",
                   ""};
        break;
    }
    case InventoryScreen::Erasing:
        message = {"ERASING TAG", {"KEEP THE TAG ON THE READER"}, "", ""};
        break;
    case InventoryScreen::RecordCreated:
        message = {"RECORD SAVED",
                   {status.record ? status.record->name : std::string(), "DESCRIBE IT ON THE MAC"},
                   "",
                   "ENTER  OK"};
        break;
    case InventoryScreen::SaveFailed:
        message = {"RECORD NOT SAVED", {"THE TAG KEEPS ITS NEW ID"}, "", "ENTER  RETRY", true};
        break;
    case InventoryScreen::Erased:
        message = {"TAG ERASED", {"THE TAG IS BLANK AGAIN"}, "", "ENTER  OK"};
        break;
    case InventoryScreen::EraseFailed:
        message = {"ERASE UNCONFIRMED",
                   {"PRESENT THE SAME TAG AGAIN",
                    status.eraseDeletesRecord ? "RECORD KEPT ON MICROSD" : "CHECK THE TAG CONTENT"},
                   "",
                   "ENTER  OK",
                   true};
        break;
    case InventoryScreen::RecordNotDeleted:
        message = {"RECORD NOT DELETED",
                   {"THE TAG IS ERASED", "DELETE IT ON THE MAC"},
                   "",
                   "ENTER  OK",
                   true};
        break;
    }
    if (hint == Hint::StorageUnavailable)
        message.lines.emplace_back("INSERT MICROSD TO SAVE");
    else if (hint == Hint::Full)
        message.lines.emplace_back("INVENTORY IS FULL");
    return message;
}

void NfcApp::render() {
    auto& view = *view_;
    const auto& status = inventory_.status();
    // Keep the previous complete frame while the next sticker is being read.
    if (status.screen == InventoryScreen::Reading && !view.editing && !view.detached &&
        view.drawnGeneration)
        return;
    if (view.drawnGeneration && *view.drawnGeneration == status.generation &&
        view.drawnPage == view.page && view.drawnEditing == view.editing &&
        view.drawnDraft == view.draft && view.drawnHint == view.hint &&
        view.drawnDetached == view.detached)
        return;
    if (view.editing) {
        drawNfcNameEntry(display_, view.recordFor ? "CREATE RECORD" : "NEW CONTAINER", view.draft,
                         inventoryMaxNameLength);
    } else if (view.retainedRecord && (status.screen == InventoryScreen::Known || view.detached)) {
        drawNfcRecord(display_, *view.retainedRecord, view.page, view.retainedRegistered,
                      !view.detached && inventory_.canErase(), view.detached);
    } else if (status.screen == InventoryScreen::Reading) {
        drawNfcMessage(display_, status.storageReady ? "" : "NO SD",
                       {"TAP A TAG", {")))"}, "", "ENTER  NEW"});
    } else {
        drawNfcMessage(display_, status.storageReady ? "" : "NO SD", message(status));
    }
    view.drawnGeneration = status.generation;
    view.drawnPage = view.page;
    view.drawnEditing = view.editing;
    view.drawnDraft = view.draft;
    view.drawnHint = view.hint;
    view.drawnDetached = view.detached;
}

} // namespace cardputer_hub::apps
