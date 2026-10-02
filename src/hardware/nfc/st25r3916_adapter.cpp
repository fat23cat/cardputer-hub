#include "hardware/nfc/st25r3916_adapter.h"
#include "hardware/nfc/nfc_startup_probe.h"

// The vendor library is confined to this translation unit. Only the reader-side
// layers are included: emulation is deliberately not used, and NDEF handling is
// project code above INfcReader.
#include <M5Unified.hpp>
#include <M5UnitUnified.hpp>
#include <nfc/layer/a/nfc_layer_a.hpp>
#include <nfc/layer/b/nfc_layer_b.hpp>
#include <nfc/layer/f/nfc_layer_f.hpp>
#include <nfc/layer/v/nfc_layer_v.hpp>
#include <unit/unit_ST25R3916.hpp>

#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstddef>
#include <optional>

namespace cardputer_hub::hardware {
namespace {

constexpr std::uint32_t probeClockHz = 100000;
// Polling budgets per technology, in milliseconds. NFC-A is polled most often.
constexpr std::uint32_t detectBudgetA = 6;
constexpr std::uint32_t detectBudgetB = 10;
constexpr std::uint32_t detectBudgetF = 20;
constexpr std::uint32_t detectBudgetV = 15;
constexpr std::uint32_t fieldResetOffUs = 5000;
// ISO 14443-3 asks for at least 5.1 ms between the field switching on and the
// first command, so cards in the field have finished powering up.
constexpr std::uint32_t fieldSettleUs = 5100;

constexpr core::NfcTechnology pollRotation[] = {
    core::NfcTechnology::NfcA, core::NfcTechnology::NfcB, core::NfcTechnology::NfcA,
    core::NfcTechnology::NfcF, core::NfcTechnology::NfcA, core::NfcTechnology::NfcV,
};

m5::nfc::NFC modeFor(core::NfcTechnology technology) {
    switch (technology) {
    case core::NfcTechnology::NfcA:
        return m5::nfc::NFC::A;
    case core::NfcTechnology::NfcB:
        return m5::nfc::NFC::B;
    case core::NfcTechnology::NfcF:
        return m5::nfc::NFC::F;
    case core::NfcTechnology::NfcV:
        return m5::nfc::NFC::V;
    }
    return m5::nfc::NFC::A;
}

// Exhaustive on purpose: a library upgrade that adds a card type fails the build
// here instead of silently reporting it as unknown.
core::NfcCardType mapType(m5::nfc::a::Type type) {
    using Library = m5::nfc::a::Type;
    switch (type) {
    case Library::MIFARE_Classic_Mini:
        return core::NfcCardType::MifareClassicMini;
    case Library::MIFARE_Classic_1K:
        return core::NfcCardType::MifareClassic1K;
    case Library::MIFARE_Classic_2K:
        return core::NfcCardType::MifareClassic2K;
    case Library::MIFARE_Classic_4K:
        return core::NfcCardType::MifareClassic4K;
    case Library::MIFARE_Ultralight:
        return core::NfcCardType::MifareUltralight;
    case Library::MIFARE_Ultralight_EV1_1:
    case Library::MIFARE_Ultralight_EV1_2:
        return core::NfcCardType::MifareUltralightEv1;
    case Library::MIFARE_Ultralight_Nano:
        return core::NfcCardType::MifareUltralightNano;
    case Library::MIFARE_UltralightC:
        return core::NfcCardType::MifareUltralightC;
    case Library::NTAG_203:
        return core::NfcCardType::Ntag203;
    case Library::NTAG_210u:
    case Library::NTAG_210:
        return core::NfcCardType::Ntag210;
    case Library::NTAG_212:
        return core::NfcCardType::Ntag212;
    case Library::NTAG_213:
        return core::NfcCardType::Ntag213;
    case Library::NTAG_215:
        return core::NfcCardType::Ntag215;
    case Library::NTAG_216:
        return core::NfcCardType::Ntag216;
    case Library::ST25TA_512B:
    case Library::ST25TA_2K:
    case Library::ST25TA_16K:
    case Library::ST25TA_64K:
        return core::NfcCardType::St25ta;
    case Library::ISO_14443_4:
        return core::NfcCardType::Iso14443Type4;
    case Library::MIFARE_Plus_2K:
    case Library::MIFARE_Plus_4K:
    case Library::MIFARE_Plus_SE:
        return core::NfcCardType::MifarePlus;
    case Library::MIFARE_DESFire_2K:
    case Library::MIFARE_DESFire_4K:
    case Library::MIFARE_DESFire_8K:
    case Library::MIFARE_DESFire_Light:
        return core::NfcCardType::MifareDesfire;
    case Library::NTAG_4XX:
        return core::NfcCardType::Ntag4xx;
    case Library::ISO_18092:
        return core::NfcCardType::Iso18092;
    case Library::Unknown:
        break;
    }
    return core::NfcCardType::Unknown;
}

core::NfcCardInfo infoFrom(const m5::nfc::a::PICC& picc) {
    core::NfcCardInfo info;
    info.technology = core::NfcTechnology::NfcA;
    info.type = mapType(picc.type);
    info.uid.assign(picc.uid, picc.uid + picc.size);
    info.atqa = picc.atqa;
    info.sak = picc.sak;
    if (const auto total = picc.totalSize())
        info.totalBytes = total;
    if (const auto user = picc.userAreaSize())
        info.userBytes = user;
    return info;
}

core::NfcCardInfo infoFrom(const m5::nfc::b::PICC& picc) {
    core::NfcCardInfo info;
    info.technology = core::NfcTechnology::NfcB;
    info.type = core::NfcCardType::Iso14443B;
    // NFC-B identifies a card by its PUPI, which is the identifier shown as UID.
    info.uid.assign(picc.pupi, picc.pupi + sizeof(picc.pupi));
    return info;
}

core::NfcCardInfo infoFrom(const m5::nfc::f::PICC& picc) {
    core::NfcCardInfo info;
    info.technology = core::NfcTechnology::NfcF;
    info.type = core::NfcCardType::Felica;
    info.uid.assign(picc.idm, picc.idm + sizeof(picc.idm));
    return info;
}

core::NfcCardInfo infoFrom(const m5::nfc::v::PICC& picc) {
    core::NfcCardInfo info;
    info.technology = core::NfcTechnology::NfcV;
    info.type = core::NfcCardType::Iso15693;
    info.uid.assign(picc.uid, picc.uid + sizeof(picc.uid));
    if (const auto total = picc.totalSize())
        info.totalBytes = total;
    return info;
}

bool sameBytes(const std::uint8_t* left, const std::uint8_t* right, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

} // namespace

// Everything the vendor library needs lives here and is created on initialize()
// and destroyed on shutdown(), so a retry always starts from a clean state.
struct St25r3916Adapter::Impl {
    struct Active {
        core::NfcActivationId id = 0;
        core::NfcTechnology technology = core::NfcTechnology::NfcA;
        core::NfcCardType type = core::NfcCardType::Unknown;
        m5::nfc::a::PICC a{};
        m5::nfc::b::PICC b{};
        m5::nfc::f::PICC f{};
        m5::nfc::v::PICC v{};
    };

    m5::unit::UnitUnified units;
    m5::unit::UnitST25R3916 unit;
    m5::nfc::NFCLayerA layerA{unit};
    m5::nfc::NFCLayerB layerB{unit};
    m5::nfc::NFCLayerF layerF{unit};
    m5::nfc::NFCLayerV layerV{unit};
    std::optional<Active> active;
    // A Mini App asked for scanning, and whether the field is actually on.
    bool fieldWanted = false;
    bool fieldOn = false;
    std::size_t rotation = 0;

    // A cheap register read that tells a silent card from a silent reader.
    bool responding() {
        std::uint8_t type = 0;
        std::uint8_t revision = 0;
        return unit.readICIdentity(type, revision);
    }

    // The library refuses to switch field mode with the field running, and a
    // mode switch turns the field back on in the new mode.
    bool ensureMode(m5::nfc::NFC mode) {
        if (unit.NFCMode() == mode)
            return true;
        (void)unit.disableField();
        return unit.configureNFCMode(mode);
    }

    // Switch the field on in NFC-A, the technology scanning starts with.
    bool startField() {
        const bool ok =
            unit.NFCMode() == m5::nfc::NFC::A ? unit.enableField() : ensureMode(m5::nfc::NFC::A);
        if (ok)
            esp_rom_delay_us(fieldSettleUs);
        return ok;
    }

    // Powering the field off and on returns every card in it to its idle state,
    // so a card left in the field after a removal is detected afresh.
    bool resetField() {
        const bool disabled = unit.disableField();
        esp_rom_delay_us(fieldResetOffUs);
        const bool enabled = unit.enableField();
        fieldOn = enabled;
        if (enabled)
            esp_rom_delay_us(fieldSettleUs);
        return disabled && enabled;
    }

    // A tag that answered an unexpected command can stay silent until the RF
    // field is cycled. The saved UID keeps the retry on the same tag.
    bool reactivateA(const m5::nfc::a::PICC& picc) {
        if (layerA.reactivate(picc))
            return true;
        return responding() && resetField() && layerA.reactivate(picc);
    }

    // The active NTAG21x of `activation`, or nullptr.
    Active* ntag(core::NfcActivationId activation) {
        if (!active || active->id != activation ||
            active->technology != core::NfcTechnology::NfcA || !core::nfcNtagUserArea(active->type))
            return nullptr;
        return &*active;
    }
};

St25r3916Adapter::St25r3916Adapter(core::Logger* logger) : logger_(logger) {}

St25r3916Adapter::~St25r3916Adapter() { shutdown(); }

void St25r3916Adapter::log(core::LogLevel level, const char* message) const {
    if (logger_ != nullptr)
        logger_->log({level, "St25r3916Adapter", message});
}

core::NfcReaderInitResult St25r3916Adapter::initialize() {
    shutdown();
    auto& bus = M5.Ex_I2C;
    if (!bus.isEnabled() || !bus.begin()) {
        // Nothing could answer on a bus that cannot start; the pins stay free.
        log(core::LogLevel::Warning, "Grove I2C bus unavailable");
        (void)bus.release();
        return core::NfcReaderInitResult::NotPresent;
    }
    // Probe before touching anything else: nothing may answer at the Unit NFC
    // address, in which case the pins are handed straight back.
    // The vendor begin() waits 50 ms and retries the chip identity read after
    // power-on. Do the same before this earlier address probe, otherwise a
    // transient NACK permanently hands the port to the Puzzle for this boot.
    if (!probeNfcAtStartup(
            [&bus] { return bus.scanID(m5::unit::UnitST25R3916::DEFAULT_ADDRESS, probeClockHz); },
            [](std::uint32_t milliseconds) { vTaskDelay(pdMS_TO_TICKS(milliseconds)); })) {
        log(core::LogLevel::Info, "reader did not answer at I2C 0x50 after startup retries");
        (void)bus.release();
        return core::NfcReaderInitResult::NotPresent;
    }
    auto impl = std::make_unique<Impl>();
    if (!impl->units.add(impl->unit, bus) || !impl->units.begin()) {
        impl.reset();
        (void)bus.release();
        log(core::LogLevel::Warning, "reader did not initialize");
        return core::NfcReaderInitResult::Failed;
    }
    // The library leaves the RF field on after begin(); it stays off until a
    // Mini App asks for scanning.
    (void)impl->unit.disableField();
    impl_ = std::move(impl);
    return core::NfcReaderInitResult::Ready;
}

void St25r3916Adapter::shutdown() {
    if (!impl_)
        return;
    (void)impl_->unit.disableField();
    impl_.reset();
    (void)M5.Ex_I2C.release();
}

void St25r3916Adapter::setFieldEnabled(bool enabled) {
    if (!impl_)
        return;
    auto& impl = *impl_;
    impl.active.reset();
    impl.rotation = 0;
    impl.fieldWanted = enabled;
    if (!enabled) {
        (void)impl.unit.disableField();
        impl.fieldOn = false;
        return;
    }
    impl.fieldOn = impl.startField();
}

core::NfcDetection St25r3916Adapter::detect() {
    core::NfcDetection detection;
    if (!impl_ || !impl_->fieldWanted)
        return detection;
    auto& impl = *impl_;
    // The service only looks for a card when it has none.
    impl.active.reset();
    if (!impl.fieldOn) {
        // The field could not be switched on earlier: try again, and tell a dead
        // reader from a failure that may pass.
        impl.fieldOn = impl.startField();
        if (!impl.fieldOn) {
            if (!impl.responding())
                detection.status = core::NfcDetectStatus::ReaderLost;
            return detection;
        }
    }

    const auto technology =
        pollRotation[impl.rotation++ % (sizeof(pollRotation) / sizeof(pollRotation[0]))];
    if (!impl.ensureMode(modeFor(technology))) {
        if (!impl.responding())
            detection.status = core::NfcDetectStatus::ReaderLost;
        return detection;
    }

    Impl::Active active;
    bool found = false;
    switch (technology) {
    case core::NfcTechnology::NfcA: {
        m5::nfc::a::PICC picc{};
        if (impl.layerA.detect(picc, detectBudgetA)) {
            // identify() classifies the card and leaves it halted. If it cannot,
            // the provisional type from the SAK stays.
            (void)impl.layerA.identify(picc);
            active.a = picc;
            detection.card = infoFrom(picc);
            found = true;
        }
        break;
    }
    case core::NfcTechnology::NfcB: {
        m5::nfc::b::PICC picc{};
        if (impl.layerB.detect(picc, 0x00, detectBudgetB)) {
            active.b = picc;
            detection.card = infoFrom(picc);
            found = true;
        }
        break;
    }
    case core::NfcTechnology::NfcF: {
        m5::nfc::f::PICC picc{};
        if (impl.layerF.detect(picc, detectBudgetF)) {
            active.f = picc;
            detection.card = infoFrom(picc);
            found = true;
        }
        break;
    }
    case core::NfcTechnology::NfcV: {
        m5::nfc::v::PICC picc{};
        if (impl.layerV.detect(picc, detectBudgetV)) {
            active.v = picc;
            detection.card = infoFrom(picc);
            found = true;
        }
        break;
    }
    }

    if (!found) {
        if (!impl.responding())
            detection.status = core::NfcDetectStatus::ReaderLost;
        return detection;
    }
    active.id = ++lastActivation_;
    active.technology = technology;
    active.type = detection.card.type;
    impl.active = active;
    detection.status = core::NfcDetectStatus::CardActivated;
    detection.activation = active.id;
    return detection;
}

core::NfcPresence St25r3916Adapter::presence(core::NfcActivationId activation) {
    if (!impl_)
        return core::NfcPresence::ReaderLost;
    auto& impl = *impl_;
    if (!impl.active || impl.active->id != activation)
        return core::NfcPresence::Removed;
    auto& active = *impl.active;

    bool here = false;
    switch (active.technology) {
    case core::NfcTechnology::NfcA:
        // Wake the halted card and select it by UID, then halt it again so the
        // next poll does not mistake it for a new arrival.
        here = impl.reactivateA(active.a);
        if (here)
            (void)impl.layerA.deactivate();
        break;
    case core::NfcTechnology::NfcB: {
        std::uint8_t atqb[m5::nfc::b::ATQB_LENGTH]{};
        std::uint16_t length = sizeof(atqb);
        here = impl.layerB.wakeup(atqb, length) &&
               sameBytes(atqb, active.b.pupi, sizeof(active.b.pupi));
        if (here)
            (void)impl.layerB.hlt(active.b.pupi);
        break;
    }
    case core::NfcTechnology::NfcF: {
        m5::nfc::f::PICC picc{};
        here = impl.layerF.detect(picc, detectBudgetF) &&
               sameBytes(picc.idm, active.f.idm, sizeof(active.f.idm));
        break;
    }
    case core::NfcTechnology::NfcV:
        here = impl.layerV.reactivate(active.v);
        if (here)
            (void)impl.layerV.deactivate();
        break;
    }
    if (here)
        return core::NfcPresence::Present;
    if (!impl.responding())
        return core::NfcPresence::ReaderLost;
    impl.active.reset();
    (void)impl.resetField();
    return core::NfcPresence::Removed;
}

core::NfcPageReadResult St25r3916Adapter::readPages(core::NfcActivationId activation,
                                                    std::uint16_t firstPage) {
    core::NfcPageReadResult result;
    result.activation = activation;
    if (!impl_) {
        result.status = core::NfcOperationStatus::ReaderLost;
        return result;
    }
    auto& impl = *impl_;
    auto* tag = impl.ntag(activation);
    const auto area = tag != nullptr ? core::nfcNtagUserArea(tag->type) : std::nullopt;
    if (tag == nullptr || firstPage + 3U >= area->firstPage + area->pageCount) {
        result.activation = impl.active ? impl.active->id : activation;
        result.status = core::NfcOperationStatus::Failed;
        return result;
    }
    // Each operation wakes the halted tag and halts it again, as presence()
    // does, so the next poll never mistakes it for a new arrival.
    if (!impl.reactivateA(tag->a)) {
        result.status = impl.responding() ? core::NfcOperationStatus::Failed
                                          : core::NfcOperationStatus::ReaderLost;
        return result;
    }
    const bool ok = impl.layerA.read16(result.data.data(), static_cast<std::uint8_t>(firstPage));
    (void)impl.layerA.deactivate();
    if (!ok) {
        result.data.fill(0);
        result.status = impl.responding() ? core::NfcOperationStatus::Failed
                                          : core::NfcOperationStatus::ReaderLost;
        return result;
    }
    result.status = core::NfcOperationStatus::Ok;
    return result;
}

core::NfcPageWriteResult St25r3916Adapter::writePage(core::NfcActivationId activation,
                                                     std::uint16_t page,
                                                     const core::NfcType2Page& data) {
    core::NfcPageWriteResult result;
    result.activation = activation;
    if (!impl_) {
        result.status = core::NfcOperationStatus::ReaderLost;
        return result;
    }
    auto& impl = *impl_;
    auto* tag = impl.ntag(activation);
    if (tag == nullptr) {
        result.activation = impl.active ? impl.active->id : activation;
        result.status = core::NfcOperationStatus::Failed;
        return result;
    }
    // Lock, capability, configuration and OTP pages are never addressed: the
    // page must lie in the user area, which the library checks once more.
    const auto area = *core::nfcNtagUserArea(tag->type);
    if (page < area.firstPage || page >= area.firstPage + area.pageCount) {
        result.status = core::NfcOperationStatus::Rejected;
        return result;
    }
    if (!impl.reactivateA(tag->a)) {
        result.status = impl.responding() ? core::NfcOperationStatus::Failed
                                          : core::NfcOperationStatus::ReaderLost;
        return result;
    }
    const bool ok = impl.layerA.write4(static_cast<std::uint8_t>(page), data.data(),
                                       static_cast<std::uint16_t>(data.size()), true);
    (void)impl.layerA.deactivate();
    if (ok) {
        result.status = core::NfcOperationStatus::Ok;
        return result;
    }
    result.status =
        impl.responding() ? core::NfcOperationStatus::Failed : core::NfcOperationStatus::ReaderLost;
    return result;
}

} // namespace cardputer_hub::hardware
