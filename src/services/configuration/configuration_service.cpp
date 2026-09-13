#include "services/configuration/configuration_service.h"

#include <algorithm>
#include <limits>

namespace cardputer_hub::services {
namespace {
const core::StorageAddress address{"hosts", "configuration"};
constexpr std::uint32_t maximumId = std::numeric_limits<std::int32_t>::max();
constexpr std::uint8_t versionOne = 1;
constexpr std::uint8_t versionTwo = 2;

void appendInteger(core::StorageBytes& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void appendString(core::StorageBytes& bytes, const std::string& value) {
    bytes.push_back(static_cast<std::uint8_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

bool isValidMetadataIdentifier(std::string_view value) {
    if (value.empty() || value.size() > ConfigurationService::maximumMetadataIdentifierLength)
        return false;
    const auto validFirst = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    const auto validRest = [&](unsigned char c) {
        return validFirst(c) || c == '.' || c == '_' || c == '-';
    };
    return validFirst(static_cast<unsigned char>(value.front())) &&
           std::all_of(value.begin() + 1, value.end(), validRest);
}

class Reader {
  public:
    explicit Reader(const core::StorageBytes& bytes) : bytes_(bytes) {}
    bool integer(std::uint32_t& value) {
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            std::uint8_t part = 0;
            if (!byte(part))
                return false;
            value |= static_cast<std::uint32_t>(part) << shift;
        }
        return true;
    }
    bool byte(std::uint8_t& value) {
        if (offset_ == bytes_.size())
            return false;
        value = bytes_[offset_++];
        return true;
    }
    bool string(std::string& value, std::size_t maximumLength) {
        std::uint8_t length = 0;
        if (!byte(length) || length > maximumLength || bytes_.size() - offset_ < length)
            return false;
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
        offset_ += length;
        return true;
    }
    bool done() const { return offset_ == bytes_.size(); }

  private:
    const core::StorageBytes& bytes_;
    std::size_t offset_ = 0;
};
} // namespace

bool ConfigurationService::validMetadataIdentifier(std::string_view value) {
    return isValidMetadataIdentifier(value);
}

bool ConfigurationService::valid(const HostConfiguration& value) {
    if (value.hosts.size() > connectivity::BluetoothService::maximumBondCount ||
        value.nextHostId == 0 || value.nextHostId > maximumId ||
        (value.bluetoothEnabled && !value.activeHost))
        return false;
    bool activeFound = !value.activeHost;
    for (std::size_t i = 0; i < value.hosts.size(); ++i) {
        const auto& host = value.hosts[i];
        if (host.id == 0 || host.id >= value.nextHostId || host.name.empty() ||
            host.name.size() > maximumNameLength ||
            std::any_of(host.name.begin(), host.name.end(),
                        [](unsigned char c) { return c < 32 || c > 126; }) ||
            std::all_of(host.name.begin(), host.name.end(), [](char c) { return c == ' '; }) ||
            std::all_of(host.bond.bytes.begin(), host.bond.bytes.end(),
                        [](auto b) { return b == 0; }))
            return false;
        if ((host.platform && !validMetadataIdentifier(*host.platform)) ||
            (host.mappingTemplate && !validMetadataIdentifier(*host.mappingTemplate)) ||
            host.capabilities.size() > maximumHostCapabilityCount)
            return false;
        for (std::size_t capability = 0; capability < host.capabilities.size(); ++capability) {
            if (!validMetadataIdentifier(host.capabilities[capability]) ||
                std::find(host.capabilities.begin(),
                          host.capabilities.begin() + static_cast<std::ptrdiff_t>(capability),
                          host.capabilities[capability]) !=
                    host.capabilities.begin() + static_cast<std::ptrdiff_t>(capability))
                return false;
        }
        if (value.activeHost == host.id)
            activeFound = true;
        for (std::size_t j = 0; j < i; ++j) {
            if (value.hosts[j].id == host.id || value.hosts[j].bond == host.bond)
                return false;
        }
    }
    return activeFound;
}

ConfigurationResult ConfigurationService::load() {
    const auto record = storage_.read(address);
    if (record.status == core::StorageReadStatus::NotFound) {
        value_ = {};
        return ConfigurationResult::Success;
    }
    if (record.status != core::StorageReadStatus::Found)
        return ConfigurationResult::StorageError;
    Reader reader(record.data);
    constexpr std::uint8_t prefix[]{'H', 'U', 'B', 'H'};
    for (const auto expected : prefix) {
        std::uint8_t actual = 0;
        if (!reader.byte(actual) || actual != expected)
            return ConfigurationResult::InvalidData;
    }
    std::uint8_t version = 0;
    if (!reader.byte(version) || (version != versionOne && version != versionTwo))
        return ConfigurationResult::InvalidData;
    HostConfiguration next;
    std::uint8_t enabled = 0, count = 0;
    std::uint32_t active = 0;
    if (!reader.byte(enabled) || enabled > 1 || !reader.byte(count) ||
        count > connectivity::BluetoothService::maximumBondCount ||
        !reader.integer(next.nextHostId) || !reader.integer(active))
        return ConfigurationResult::InvalidData;
    next.bluetoothEnabled = enabled != 0;
    if (active != 0)
        next.activeHost = active;
    for (std::uint8_t index = 0; index < count; ++index) {
        HostProfile host;
        if (!reader.integer(host.id) || !reader.string(host.name, maximumNameLength))
            return ConfigurationResult::InvalidData;
        for (auto& b : host.bond.bytes) {
            if (!reader.byte(b))
                return ConfigurationResult::InvalidData;
        }
        if (version == versionTwo) {
            std::string platform;
            std::uint8_t capabilityCount = 0;
            if (!reader.string(platform, maximumMetadataIdentifierLength) ||
                !reader.byte(capabilityCount) || capabilityCount > maximumHostCapabilityCount)
                return ConfigurationResult::InvalidData;
            if (!platform.empty())
                host.platform = std::move(platform);
            for (std::uint8_t capability = 0; capability < capabilityCount; ++capability) {
                std::string id;
                if (!reader.string(id, maximumMetadataIdentifierLength))
                    return ConfigurationResult::InvalidData;
                host.capabilities.push_back(std::move(id));
            }
            std::string mappingTemplate;
            if (!reader.string(mappingTemplate, maximumMetadataIdentifierLength))
                return ConfigurationResult::InvalidData;
            if (!mappingTemplate.empty())
                host.mappingTemplate = std::move(mappingTemplate);
        }
        next.hosts.push_back(std::move(host));
    }
    if (!reader.done() || !valid(next))
        return ConfigurationResult::InvalidData;
    value_ = std::move(next);
    return ConfigurationResult::Success;
}

ConfigurationResult ConfigurationService::save(const HostConfiguration& value) {
    if (!valid(value))
        return ConfigurationResult::InvalidData;
    core::StorageBytes bytes{'H',
                             'U',
                             'B',
                             'H',
                             versionTwo,
                             static_cast<std::uint8_t>(value.bluetoothEnabled),
                             static_cast<std::uint8_t>(value.hosts.size())};
    bytes.reserve(maximumSerializedSize);
    appendInteger(bytes, value.nextHostId);
    appendInteger(bytes, value.activeHost.value_or(0));
    for (const auto& host : value.hosts) {
        appendInteger(bytes, host.id);
        appendString(bytes, host.name);
        bytes.insert(bytes.end(), host.bond.bytes.begin(), host.bond.bytes.end());
        appendString(bytes, host.platform.value_or(""));
        bytes.push_back(static_cast<std::uint8_t>(host.capabilities.size()));
        for (const auto& capability : host.capabilities)
            appendString(bytes, capability);
        appendString(bytes, host.mappingTemplate.value_or(""));
    }
    if (storage_.write(address, bytes) != core::StorageWriteStatus::Stored)
        return ConfigurationResult::StorageError;
    value_ = value;
    return ConfigurationResult::Success;
}
} // namespace cardputer_hub::services
