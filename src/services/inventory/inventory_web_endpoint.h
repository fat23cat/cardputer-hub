#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "services/inventory/inventory_service.h"
#include "services/web_access/web_app.h"

namespace cardputer_hub::services {

// The Inventory app of the local web UI. Records travel as their canonical
// JSON, exactly as over the Companion: an upload is the record at the revision
// the edit started from, decoded with decodeInventoryRecord() and committed
// by InventoryService as the next revision. Nothing is kept between requests.
//
//   GET    /records?after=<id>        a page of records in ID order
//   GET    /records/<id>              one record
//   PUT    /records/<id>              commit an edit
//   DELETE /records/<id>?revision=N   remove (0 for a damaged file)
class InventoryWebEndpoint final : public IWebApp {
  public:
    static constexpr std::size_t maxPageRecords = 16;
    static constexpr std::size_t maxPageBytes = 6 * 1024;

    explicit InventoryWebEndpoint(InventoryService& inventory) : inventory_(inventory) {}

    [[nodiscard]] std::string_view id() const override { return "inventory"; }
    [[nodiscard]] std::string_view title() const override { return "INVENTORY"; }
    [[nodiscard]] std::string summaryJson() override;
    [[nodiscard]] connectivity::HttpResponse handle(const connectivity::HttpRequest& request,
                                                    std::string_view subpath) override;

  private:
    connectivity::HttpResponse list(const connectivity::HttpRequest& request);
    connectivity::HttpResponse get(const InventoryId& id);
    connectivity::HttpResponse put(const InventoryId& id, const connectivity::HttpRequest& request);
    connectivity::HttpResponse remove(const InventoryId& id,
                                      const connectivity::HttpRequest& request);

    InventoryService& inventory_;
};

} // namespace cardputer_hub::services
