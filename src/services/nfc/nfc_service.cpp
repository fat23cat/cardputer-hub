#include "services/nfc/nfc_service.h"

#include "services/nfc/nfc_ndef.h"

#include <algorithm>
#include <utility>

namespace cardputer_hub::services {
namespace {

constexpr char logComponent[] = "NfcService";
constexpr std::uint8_t maxOperationRetries = 1;

std::size_t readsFor(std::size_t bytes) {
    return (bytes + core::nfcType2ReadBytes - 1) / core::nfcType2ReadBytes;
}

} // namespace

NfcService::NfcService(core::INfcReader& reader, core::CapabilityRegistry& capabilities,
                       core::Logger* logger, NfcServiceConfig config)
    : reader_(reader), capabilities_(capabilities), logger_(logger), config_(config) {}

void NfcService::log(core::LogLevel level, const char* message) const {
    if (logger_ != nullptr)
        logger_->log({level, logComponent, message});
}

void NfcService::touch() { ++status_.generation; }

void NfcService::setState(NfcServiceState state) {
    if (status_.state == state)
        return;
    status_.state = state;
    touch();
}

bool NfcService::readerReady() const noexcept {
    switch (status_.state) {
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

bool NfcService::start() {
    if (started_)
        return readerReady();
    started_ = true;
    initializeReader(true);
    return readerReady();
}

void NfcService::initializeReader(bool initial) {
    setState(NfcServiceState::Initializing);
    sinceRetry_ = std::chrono::milliseconds(0);
    switch (reader_.initialize()) {
    case core::NfcReaderInitResult::Ready:
        readerPresent_ = true;
        retryAllowed_ = true;
        if (!capabilityPublished_) {
            (void)capabilities_.registerCapability(nfcReaderCapabilityId);
            capabilityPublished_ = true;
        }
        setState(NfcServiceState::Idle);
        if (scanning_) {
            reader_.setFieldEnabled(true);
            sincePoll_ = config_.pollInterval;
        }
        log(core::LogLevel::Info, "reader ready");
        break;
    case core::NfcReaderInitResult::NotPresent:
        // A reader that never answered is not probed again: its bus pins may
        // belong to another device. One that answered before is, because
        // retrying only touches pins the reader already owns.
        if (initial) {
            readerPresent_ = false;
            retryAllowed_ = false;
            log(core::LogLevel::Info, "reader not present");
        }
        setState(NfcServiceState::Unavailable);
        break;
    case core::NfcReaderInitResult::Failed:
        readerPresent_ = true;
        retryAllowed_ = true;
        setState(NfcServiceState::Error);
        log(core::LogLevel::Warning, "reader initialization failed");
        break;
    }
}

void NfcService::update(std::chrono::milliseconds elapsed) {
    if (!started_)
        return;
    switch (status_.state) {
    case NfcServiceState::Unavailable:
    case NfcServiceState::Error:
        if (retryAllowed_) {
            sinceRetry_ += elapsed;
            if (sinceRetry_ >= config_.retryInterval)
                initializeReader(false);
        }
        return;
    case NfcServiceState::Initializing:
        return;
    case NfcServiceState::Idle:
        if (!scanning_)
            return;
        sincePoll_ += elapsed;
        if (sincePoll_ >= config_.pollInterval) {
            sincePoll_ = std::chrono::milliseconds(0);
            poll();
        }
        return;
    case NfcServiceState::Reading:
        readStep();
        return;
    case NfcServiceState::Writing:
        if (session_->verifying)
            verifyStep();
        else
            writeStep();
        return;
    case NfcServiceState::Ready:
        sincePresence_ += elapsed;
        if (sincePresence_ >= config_.presenceInterval) {
            sincePresence_ = std::chrono::milliseconds(0);
            checkPresence();
        }
        return;
    }
}

void NfcService::startScanning() {
    if (scanning_)
        return;
    scanning_ = true;
    status_.scanning = true;
    // The first poll after scanning starts does not wait out an interval.
    sincePoll_ = config_.pollInterval;
    if (readerReady())
        reader_.setFieldEnabled(true);
    touch();
}

void NfcService::stopScanning() {
    if (!scanning_)
        return;
    scanning_ = false;
    status_.scanning = false;
    interruptWrite();
    session_.reset();
    clearTagStatus();
    if (readerReady()) {
        reader_.setFieldEnabled(false);
        setState(NfcServiceState::Idle);
    }
    touch();
}

bool NfcService::refuseWrite(std::uint32_t session) {
    status_.write = NfcWriteState::Refused;
    status_.writeSession = session;
    touch();
    return false;
}

bool NfcService::writeMessage(std::uint32_t session, const std::vector<std::uint8_t>& message) {
    if (!session_ || session_->id != session || status_.state != NfcServiceState::Ready ||
        status_.tag.content != NfcTagContent::Blank || !status_.tag.writable || !session_->userArea)
        return refuseWrite(session);
    const auto userBytes = static_cast<std::size_t>(session_->userArea->pageCount) * 4U;
    const auto capacity = std::min<std::size_t>(status_.tag.capacity, userBytes);
    auto area = encodeType2NdefArea(message, capacity);
    if (!area)
        return refuseWrite(session);
    beginWrite(session, std::move(*area));
    return true;
}

bool NfcService::isErasable(const NfcTagInspection& tag) noexcept {
    return (tag.content == NfcTagContent::Message || tag.content == NfcTagContent::OtherData) &&
           tag.writable && !tag.reserved && !tag.raw.empty();
}

bool NfcService::eraseTag(std::uint32_t session, const std::vector<std::uint8_t>& expectedRaw) {
    if (!session_ || session_->id != session || status_.state != NfcServiceState::Ready ||
        !isErasable(status_.tag) || status_.tag.raw != expectedRaw || !session_->userArea)
        return refuseWrite(session);
    beginWrite(session, {0x03, 0x00, 0xFE, 0x00});
    return true;
}

// NFC Forum Type 2 write order: an empty NDEF TLV and a Terminator first, the
// message body next, and the real first page last. An interrupted write leaves
// a provably blank tag or a complete message, never a truncated message that
// parses. Erasing is the first step alone.
void NfcService::beginWrite(std::uint32_t session, std::vector<std::uint8_t> area) {
    auto& plan = *session_;
    plan.writes.clear();
    const auto first = core::nfcType2FirstUserPage;
    const core::NfcType2Page blank{0x03, 0x00, 0xFE, 0x00};
    if (area.size() > 4)
        plan.writes.push_back({first, blank});
    for (std::size_t offset = 4; offset < area.size(); offset += 4) {
        plan.writes.push_back(
            {static_cast<std::uint16_t>(first + offset / 4),
             {area[offset], area[offset + 1], area[offset + 2], area[offset + 3]}});
    }
    plan.writes.push_back({first, {area[0], area[1], area[2], area[3]}});
    plan.writeIndex = 0;
    plan.writeRetries = 0;
    plan.area = std::move(area);
    plan.readBack.clear();
    plan.verifying = false;
    status_.write = NfcWriteState::Writing;
    status_.writeSession = session;
    setState(NfcServiceState::Writing);
    touch();
}

// ---- Tag session ----------------------------------------------------------------

void NfcService::poll() {
    const auto detection = reader_.detect();
    switch (detection.status) {
    case core::NfcDetectStatus::NoCard:
        return;
    case core::NfcDetectStatus::ReaderLost:
        handleReaderLost();
        return;
    case core::NfcDetectStatus::CardActivated:
        break;
    }
    Session session;
    session.id = nextSession_++;
    session.activation = detection.activation;
    if (detection.card.technology == core::NfcTechnology::NfcA)
        session.userArea = core::nfcNtagUserArea(detection.card.type);
    status_.session = session.id;
    status_.card = detection.card;
    status_.tag = {};
    session_ = std::move(session);
    touch();
    if (!session_->userArea) {
        // Recognising a card family is not inventory compatibility: nothing
        // beyond its identification is ever read.
        status_.tag.content = NfcTagContent::Unsupported;
        sincePresence_ = std::chrono::milliseconds(0);
        setState(NfcServiceState::Ready);
        return;
    }
    beginInspection();
}

void NfcService::beginInspection() {
    session_->window.clear();
    session_->readRetries = 0;
    status_.tag = {};
    setState(NfcServiceState::Reading);
    touch();
}

void NfcService::checkPresence() {
    switch (reader_.presence(session_->activation)) {
    case core::NfcPresence::Present:
        return;
    case core::NfcPresence::Removed:
        endSession();
        return;
    case core::NfcPresence::ReaderLost:
        handleReaderLost();
        return;
    }
}

bool NfcService::confirmPresent() {
    switch (reader_.presence(session_->activation)) {
    case core::NfcPresence::Present:
        return true;
    case core::NfcPresence::Removed:
        endSession();
        return false;
    case core::NfcPresence::ReaderLost:
        handleReaderLost();
        return false;
    }
    return false;
}

// Reads the first window, then more of the data area while its content is not
// decided yet. The last group is read ending at the last user page, so no read
// leaves the user area.
void NfcService::readStep() {
    auto& session = *session_;
    const auto next = static_cast<std::uint16_t>(nfcInspectFirstPage + session.window.size() / 4U);
    const auto lastUserPage =
        static_cast<std::uint16_t>(session.userArea->firstPage + session.userArea->pageCount - 1U);
    const auto first = std::min<std::uint16_t>(next, static_cast<std::uint16_t>(lastUserPage - 3U));
    const auto result = reader_.readPages(session.activation, first);
    if (!acceptActivation(result.activation))
        return;
    switch (result.status) {
    case core::NfcOperationStatus::Ok: {
        session.readRetries = 0;
        const auto skip = static_cast<std::size_t>(next - first) * 4U;
        session.window.insert(session.window.end(), result.data.begin() + skip, result.data.end());
        if (session.window.size() < nfcInspectBytes)
            return;
        auto inspection = inspectType2Window(session.window.data(), session.window.size());
        if (inspection.incomplete) {
            if (nfcInspectFirstPage + session.window.size() / 4U <= lastUserPage)
                return;
            // A capability container claiming more than the user area: the tag
            // cannot be proven blank, so it is never written.
            inspection.incomplete = false;
            inspection.content = NfcTagContent::OtherData;
        }
        status_.tag = std::move(inspection);
        sincePresence_ = std::chrono::milliseconds(0);
        setState(NfcServiceState::Ready);
        touch();
        return;
    }
    case core::NfcOperationStatus::Rejected:
    case core::NfcOperationStatus::Failed:
        // A failure may mean the tag left: confirm before retrying.
        if (!confirmPresent())
            return;
        if (session.readRetries++ < maxOperationRetries)
            return;
        status_.tag = {};
        status_.tag.content = NfcTagContent::ReadFailed;
        sincePresence_ = std::chrono::milliseconds(0);
        setState(NfcServiceState::Ready);
        touch();
        return;
    case core::NfcOperationStatus::ReaderLost:
        handleReaderLost();
        return;
    }
}

void NfcService::writeStep() {
    auto& session = *session_;
    const auto& [page, data] = session.writes[session.writeIndex];
    const auto result = reader_.writePage(session.activation, page, data);
    if (!acceptActivation(result.activation))
        return;
    switch (result.status) {
    case core::NfcOperationStatus::Ok:
        session.writeRetries = 0;
        if (++session.writeIndex >= session.writes.size()) {
            session.verifying = true;
            session.readBack.clear();
            status_.write = NfcWriteState::Verifying;
            touch();
        }
        return;
    case core::NfcOperationStatus::Failed:
        if (!confirmPresent())
            return;
        if (session.writeRetries++ < maxOperationRetries)
            return;
        finishWrite(NfcWriteState::Failed);
        return;
    case core::NfcOperationStatus::Rejected:
        finishWrite(NfcWriteState::Failed);
        return;
    case core::NfcOperationStatus::ReaderLost:
        handleReaderLost();
        return;
    }
}

void NfcService::verifyStep() {
    auto& session = *session_;
    const auto page =
        static_cast<std::uint16_t>(core::nfcType2FirstUserPage + session.readBack.size() / 4U);
    const auto result = reader_.readPages(session.activation, page);
    if (!acceptActivation(result.activation))
        return;
    switch (result.status) {
    case core::NfcOperationStatus::Ok:
        session.readBack.insert(session.readBack.end(), result.data.begin(), result.data.end());
        session.writeRetries = 0;
        if (session.readBack.size() < readsFor(session.area.size()) * core::nfcType2ReadBytes)
            return;
        if (!std::equal(session.area.begin(), session.area.end(), session.readBack.begin())) {
            finishWrite(NfcWriteState::Failed);
            return;
        }
        // The verified bytes replace what was inspected there, so the published
        // content and its raw bytes describe the tag as it is now.
        if (session.window.size() < 8 + session.area.size())
            session.window.resize(8 + session.area.size(), 0);
        std::copy(session.area.begin(), session.area.end(), session.window.begin() + 8);
        status_.tag = inspectType2Window(session.window.data(), session.window.size());
        status_.tag.incomplete = false;
        status_.write = NfcWriteState::Succeeded;
        session.writes.clear();
        session.verifying = false;
        sincePresence_ = std::chrono::milliseconds(0);
        setState(NfcServiceState::Ready);
        touch();
        return;
    case core::NfcOperationStatus::Rejected:
    case core::NfcOperationStatus::Failed:
        if (!confirmPresent())
            return;
        if (session.writeRetries++ < maxOperationRetries)
            return;
        finishWrite(NfcWriteState::Failed);
        return;
    case core::NfcOperationStatus::ReaderLost:
        handleReaderLost();
        return;
    }
}

// A failed write may have changed the tag: its data area is read again, so the
// published content never describes what the tag held before the write.
void NfcService::finishWrite(NfcWriteState result) {
    auto& session = *session_;
    session.writes.clear();
    session.verifying = false;
    status_.write = result;
    log(core::LogLevel::Warning, "tag write failed");
    beginInspection();
}

// A result is accepted only when it belongs to the current tag. A result for a
// newer activation proves the reader moved on, so this session's tag is gone;
// a result for an earlier activation is a late completion and is discarded.
bool NfcService::acceptActivation(core::NfcActivationId activation) {
    auto& session = *session_;
    if (activation == session.activation)
        return true;
    if (activation > session.activation) {
        endSession();
        return false;
    }
    if (++session.staleResults >= config_.maxStaleResults) {
        log(core::LogLevel::Warning, "repeated stale results, resetting reader");
        handleReaderLost();
    }
    return false;
}

void NfcService::interruptWrite() {
    if (status_.state == NfcServiceState::Writing && session_) {
        status_.write = NfcWriteState::Interrupted;
        status_.writeSession = session_->id;
    }
}

void NfcService::endSession() {
    interruptWrite();
    session_.reset();
    clearTagStatus();
    sincePoll_ = std::chrono::milliseconds(0);
    setState(NfcServiceState::Idle);
    touch();
}

void NfcService::handleReaderLost() {
    interruptWrite();
    session_.reset();
    clearTagStatus();
    reader_.shutdown();
    if (capabilityPublished_) {
        (void)capabilities_.removeCapability(nfcReaderCapabilityId);
        capabilityPublished_ = false;
    }
    retryAllowed_ = true;
    sinceRetry_ = std::chrono::milliseconds(0);
    setState(NfcServiceState::Error);
    touch();
    log(core::LogLevel::Warning, "reader lost");
}

void NfcService::clearTagStatus() {
    status_.session = 0;
    status_.card.reset();
    status_.tag = {};
}

} // namespace cardputer_hub::services
