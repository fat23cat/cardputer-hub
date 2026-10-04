#pragma once

#include "core/nfc/nfc_reader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace cardputer_hub::test_support {

// A tag that can be placed in the FakeNfcReader's field. NTAG tags carry their
// whole page memory; other cards carry only identification. Every value here
// is synthetic test data.
struct FakeCard {
    core::NfcCardInfo info;
    std::vector<std::uint8_t> memory;
    // Pages that refuse writes, as locked pages do.
    std::set<std::uint16_t> lockedPages;
    // Set by the reader when the card is activated.
    core::NfcActivationId activation = 0;
    bool reported = false;
};

class FakeNfcReader final : public core::INfcReader {
  public:
    struct PageWrite {
        core::NfcActivationId activation = 0;
        std::uint16_t page = 0;
        core::NfcType2Page data{};
    };

    // ---- Script ----------------------------------------------------------
    // Consumed in order by initialize(); the last entry repeats.
    std::vector<core::NfcReaderInitResult> initResults{core::NfcReaderInitResult::Ready};
    // Every operation reports ReaderLost while set.
    bool readerGone = false;
    // The next `staleOperations` read/write results echo this activation.
    core::NfcActivationId staleActivation = 0;
    int staleOperations = 0;
    // Communication failures for the next N reads or writes.
    int failReads = 0;
    int failWrites = 0;
    // The card leaves the field after this many further successful writes.
    std::optional<int> removeAfterWrites;
    // A write to this page stores different data, as a faulty tag would.
    std::optional<std::uint16_t> corruptPage;

    void present(const FakeCard& next) {
        card_ = next;
        card_->activation = 0;
        card_->reported = false;
    }
    void removeCard() {
        if (card_)
            departed_ = card_->memory;
        card_.reset();
    }
    [[nodiscard]] bool cardPresent() const { return card_.has_value(); }
    // Memory of the card in the field, or of the one that left most recently.
    [[nodiscard]] const std::vector<std::uint8_t>& memory() const {
        return card_ ? card_->memory : departed_;
    }

    // ---- Observation -----------------------------------------------------
    int initializeCalls = 0;
    int shutdownCalls = 0;
    int detectCalls = 0;
    int presenceCalls = 0;
    bool fieldEnabled = false;
    std::vector<std::uint16_t> pageReads;
    std::vector<PageWrite> pageWrites;
    core::NfcActivationId lastActivation = 0;

    // ---- INfcReader ------------------------------------------------------
    core::NfcReaderInitResult initialize() override {
        const auto index = static_cast<std::size_t>(initializeCalls);
        ++initializeCalls;
        const auto result =
            initResults[index < initResults.size() ? index : initResults.size() - 1];
        if (result == core::NfcReaderInitResult::Ready)
            readerGone = false;
        return result;
    }
    void shutdown() override {
        ++shutdownCalls;
        fieldEnabled = false;
        // A reader that was shut down forgets its activation; a card that is still
        // in the field is activated afresh by the next detect().
        if (card_) {
            card_->reported = false;
            card_->activation = 0;
        }
        pendingDetection_ = false;
    }
    void setFieldEnabled(bool enabled) override { fieldEnabled = enabled; }

    core::NfcDetection detect() override {
        ++detectCalls;
        core::NfcDetection detection;
        if (readerGone) {
            detection.status = core::NfcDetectStatus::ReaderLost;
            return detection;
        }
        if (!fieldEnabled || !card_)
            return detection;
        if (pendingDetection_) {
            // The reader activated this card while serving an earlier operation.
            pendingDetection_ = false;
            detection.status = core::NfcDetectStatus::CardActivated;
            detection.activation = card_->activation;
            detection.card = card_->info;
            return detection;
        }
        if (card_->reported)
            return detection;
        activate(*card_);
        detection.status = core::NfcDetectStatus::CardActivated;
        detection.activation = card_->activation;
        detection.card = card_->info;
        return detection;
    }

    core::NfcPresence presence(core::NfcActivationId activation) override {
        ++presenceCalls;
        if (readerGone)
            return core::NfcPresence::ReaderLost;
        if (!card_ || card_->activation != activation)
            return core::NfcPresence::Removed;
        return core::NfcPresence::Present;
    }

    core::NfcPageReadResult readPages(core::NfcActivationId activation,
                                      std::uint16_t firstPage) override {
        pageReads.push_back(firstPage);
        core::NfcPageReadResult result;
        result.activation = echoed(activation);
        if (readerGone) {
            result.status = core::NfcOperationStatus::ReaderLost;
            return result;
        }
        adoptSwappedCard(activation, result.activation);
        if (!card_ || card_->activation != activation || card_->memory.empty()) {
            result.status = core::NfcOperationStatus::Failed;
            return result;
        }
        if (failReads > 0) {
            --failReads;
            result.status = core::NfcOperationStatus::Failed;
            return result;
        }
        const std::size_t pages = card_->memory.size() / 4;
        for (std::size_t index = 0; index < core::nfcType2ReadBytes; ++index) {
            // NTAG READ wraps around the end of memory.
            const auto offset = (firstPage * 4U + index) % (pages * 4U);
            result.data[index] = card_->memory[offset];
        }
        result.status = core::NfcOperationStatus::Ok;
        return result;
    }

    core::NfcPageWriteResult writePage(core::NfcActivationId activation, std::uint16_t page,
                                       const core::NfcType2Page& data) override {
        pageWrites.push_back({activation, page, data});
        core::NfcPageWriteResult result;
        result.activation = echoed(activation);
        if (readerGone) {
            result.status = core::NfcOperationStatus::ReaderLost;
            return result;
        }
        adoptSwappedCard(activation, result.activation);
        if (!card_ || card_->activation != activation) {
            result.status = core::NfcOperationStatus::Failed;
            return result;
        }
        const auto area = core::nfcNtagUserArea(card_->info.type);
        // The real adapter refuses pages outside the user area without
        // touching the tag; a locked page refuses the write.
        if (!area || page < area->firstPage || page >= area->firstPage + area->pageCount ||
            card_->lockedPages.count(page) != 0) {
            result.status = core::NfcOperationStatus::Rejected;
            return result;
        }
        if (failWrites > 0) {
            --failWrites;
            result.status = core::NfcOperationStatus::Failed;
            return result;
        }
        auto stored = data;
        if (corruptPage && *corruptPage == page)
            stored[3] ^= 0x5A;
        std::copy(stored.begin(), stored.end(), card_->memory.begin() + page * 4);
        result.status = core::NfcOperationStatus::Ok;
        if (removeAfterWrites && --*removeAfterWrites <= 0) {
            removeAfterWrites.reset();
            removeCard();
        }
        return result;
    }

  private:
    void activate(FakeCard& card) {
        card.activation = ++lastActivation;
        card.reported = true;
    }

    core::NfcActivationId echoed(core::NfcActivationId activation) {
        if (staleOperations > 0) {
            --staleOperations;
            return staleActivation;
        }
        return activation;
    }

    // Mirrors the real adapter: an operation requested for a card that has been
    // replaced is not run against the newcomer. The reader activates the newcomer
    // and reports the result under the new activation instead.
    void adoptSwappedCard(core::NfcActivationId requested, core::NfcActivationId& reported) {
        if (card_ && card_->activation != requested && !card_->reported) {
            activate(*card_);
            pendingDetection_ = true;
            reported = card_->activation;
        }
    }

    std::optional<FakeCard> card_;
    std::vector<std::uint8_t> departed_;
    bool pendingDetection_ = false;
};

// A factory-fresh NTAG213: capability container E1 10 12 00 and an empty NDEF
// TLV. The UID's last byte is `seed`.
inline FakeCard makeNtag213(std::uint8_t seed) {
    FakeCard card;
    card.info.technology = core::NfcTechnology::NfcA;
    card.info.type = core::NfcCardType::Ntag213;
    card.info.uid = {0x04, 0x51, 0x7A, 0x22, 0x61, 0x10, seed};
    card.info.atqa = 0x0044;
    card.info.sak = 0x00;
    card.info.totalBytes = 180;
    card.info.userBytes = 144;
    card.memory.assign(45 * 4, 0);
    for (std::size_t index = 0; index < 7; ++index)
        card.memory[index < 3 ? index : index + 1] = card.info.uid[index];
    const std::uint8_t cc[] = {0xE1, 0x10, 0x12, 0x00};
    std::copy(std::begin(cc), std::end(cc), card.memory.begin() + 12);
    const std::uint8_t emptyNdef[] = {0x03, 0x00, 0xFE, 0x00};
    std::copy(std::begin(emptyNdef), std::end(emptyNdef), card.memory.begin() + 16);
    return card;
}

// An NTAG213 holding `area` (TLV bytes) from page 4.
inline FakeCard makeNtag213WithArea(std::uint8_t seed, const std::vector<std::uint8_t>& area) {
    auto card = makeNtag213(seed);
    std::copy(area.begin(), area.end(), card.memory.begin() + 16);
    return card;
}

inline FakeCard makeClassic1K(std::uint8_t seed) {
    FakeCard card;
    card.info.technology = core::NfcTechnology::NfcA;
    card.info.type = core::NfcCardType::MifareClassic1K;
    card.info.uid = {0x04, 0xA8, 0x12, seed};
    card.info.atqa = 0x0004;
    card.info.sak = 0x08;
    card.info.totalBytes = 1024;
    return card;
}

inline FakeCard makeNfcB(std::uint8_t seed) {
    FakeCard card;
    card.info.technology = core::NfcTechnology::NfcB;
    card.info.type = core::NfcCardType::Iso14443B;
    card.info.uid = {0xDE, 0xAD, 0xBE, seed};
    return card;
}

} // namespace cardputer_hub::test_support
