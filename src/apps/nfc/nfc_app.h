#pragma once

#include "apps/nfc/nfc_graphics.h"
#include "apps/runtime/mini_app.h"
#include "core/display/display_adapter.h"
#include "services/inventory/inventory_service.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace cardputer_hub::apps {

// The NFC inventory screen. It renders InventoryService state and handles keys:
// it never drives the reader or touches storage, and it owns only the view
// (page, name draft, hints).
class NfcApp final : public IMiniApp {
  public:
    NfcApp(services::InventoryService& inventory, core::IDisplayAdapter& display)
        : inventory_(inventory), display_(display) {}

    void onActivate() override;
    void onDeactivate() override;
    void update(const core::InputEvents& input, std::chrono::milliseconds elapsed) override;
    bool handleBack() override;

  private:
    enum class Hint : std::uint8_t { None, StorageUnavailable, Full };

    struct View {
        std::size_t page = 0;
        // Name entry runs without a tag: for a new container, or for the
        // record of `recordFor` (an existing tag ID).
        bool editing = false;
        std::optional<services::InventoryId> recordFor;
        std::string draft;
        Hint hint = Hint::None;
        // The inventory state this view belongs to.
        std::uint32_t session = 0;
        services::InventoryScreen screen = services::InventoryScreen::ReaderUnavailable;
        // What the last frame showed; an unchanged view repaints nothing.
        std::optional<std::uint32_t> drawnGeneration;
        std::size_t drawnPage = 0;
        bool drawnEditing = false;
        std::string drawnDraft;
        Hint drawnHint = Hint::None;
    };

    void handle(const core::InputEvent& event);
    void handleEditor(const core::InputEvent& event);
    void startEditing(std::optional<services::InventoryId> recordFor);
    void submitName();
    void syncWithInventory();
    void render();
    [[nodiscard]] NfcMessage message(const services::InventoryStatus& status) const;

    services::InventoryService& inventory_;
    core::IDisplayAdapter& display_;
    std::optional<View> view_;
};

} // namespace cardputer_hub::apps
