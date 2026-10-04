#pragma once

#include "services/companion/companion_service.h"
#include "services/inventory/inventory_service.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace cardputer_hub::services {

// Answers the Mac's inventory requests over the live Companion session.
//
// Records travel as canonical JSON in bounded chunks. A download is served from
// one snapshot and every chunk names its revision. An upload is buffered (at
// most one, at most 4 KiB, only while it is in progress) and becomes a record
// only when InventoryService validates and commits the complete JSON. A new or
// lost session, an out-of-order chunk or ten idle seconds discard it; nothing
// is ever replayed.
class InventoryCompanionEndpoint {
  public:
    static constexpr auto uploadIdleTimeout = std::chrono::seconds(10);
    static_assert(connectivity::companionInventoryMaxRecordBytes == inventoryMaxRecordBytes,
                  "the wire bound is the record bound");

    InventoryCompanionEndpoint(CompanionService& companion, InventoryService& inventory)
        : companion_(companion), inventory_(inventory) {}

    void update(std::chrono::milliseconds elapsed);

    [[nodiscard]] bool uploadInProgress() const noexcept { return upload_.has_value(); }

  private:
    struct Upload {
        InventoryId id{};
        std::uint32_t expectedRevision = 0;
        std::uint16_t total = 0;
        std::string data;
        std::chrono::milliseconds idle{0};
    };
    struct Download {
        InventoryId id{};
        std::uint32_t revision = 0;
        std::string json;
    };

    void handle(const CompanionInboundRequest& request);
    void handleList(const CompanionInboundRequest& request);
    void handleGet(const CompanionInboundRequest& request);
    void handlePut(const CompanionInboundRequest& request);
    void handleDelete(const CompanionInboundRequest& request);
    void reply(const CompanionInboundRequest& request, connectivity::CompanionStatus status);
    void replyStore(const CompanionInboundRequest& request, InventoryStoreStatus status,
                    std::uint32_t revision);

    CompanionService& companion_;
    InventoryService& inventory_;
    std::uint32_t epoch_ = 0;
    std::optional<Upload> upload_;
    std::optional<Download> download_;
};

} // namespace cardputer_hub::services
