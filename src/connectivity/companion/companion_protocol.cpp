#include "connectivity/companion/companion_protocol.h"

#include <cstring>

namespace cardputer_hub::connectivity {
namespace {

bool isKnownKindValue(std::uint8_t kind) noexcept {
    return kind >= static_cast<std::uint8_t>(CompanionKind::Hello) &&
           kind <= static_cast<std::uint8_t>(CompanionKind::Event);
}

bool isKnownOperationValue(std::uint8_t operation) noexcept {
    return operation <= static_cast<std::uint8_t>(CompanionOperation::AiUsage);
}

bool isKnownStatusValue(std::uint8_t status) noexcept {
    return status <= static_cast<std::uint8_t>(CompanionStatus::Malformed);
}

bool operationAllowedForKind(CompanionKind kind, CompanionOperation operation) noexcept;

bool headerSemanticsValid(const CompanionEnvelope& message) noexcept {
    switch (message.kind) {
    case CompanionKind::Hello:
        return message.session == 0 && message.requestId == 0 &&
               message.operation == CompanionOperation::None &&
               message.status == CompanionStatus::Ok;
    case CompanionKind::HelloAck:
        return message.session != 0 && message.requestId == 0 &&
               message.operation == CompanionOperation::None &&
               message.status == CompanionStatus::Ok;
    case CompanionKind::Request:
        return message.session != 0 && message.requestId != 0 &&
               message.status == CompanionStatus::Ok;
    case CompanionKind::Response:
        return message.session != 0 && message.requestId != 0;
    case CompanionKind::Event:
        return message.session != 0 && message.requestId == 0 &&
               message.status == CompanionStatus::Ok;
    }
    return false;
}

bool helloPayloadValid(const CompanionEnvelope& message) noexcept {
    return message.payloadSize >= 2 && message.payload[0] > 0 &&
           message.payload[0] <= companionMaxSupportedVersions &&
           message.payloadSize == static_cast<std::uint8_t>(message.payload[0] + 1);
}

bool bundlePayloadValid(const CompanionEnvelope& message) noexcept {
    char bundle[companionMaxBundleIdSize + 1]{};
    std::uint8_t length = 0;
    return readBundleIdentifier(message, bundle, sizeof(bundle), length);
}

bool capabilitiesPayloadValid(const CompanionEnvelope& message) noexcept {
    CompanionCapability capabilities[companionMaxCapabilities]{};
    std::uint8_t count = 0;
    return readCapabilityList(message, capabilities, count, companionMaxCapabilities);
}

bool operationPayloadValid(const CompanionEnvelope& message) noexcept {
    switch (message.operation) {
    case CompanionOperation::None:
        if (message.kind == CompanionKind::Hello) {
            return helloPayloadValid(message);
        }
        return message.kind == CompanionKind::HelloAck && message.payloadSize == 1 &&
               message.payload[0] >= companionProtocolVersion &&
               message.payload[0] <= companionLatestProtocolVersion;
    case CompanionOperation::Ping:
        return message.payloadSize == companionPingTokenSize;
    case CompanionOperation::Capabilities:
        if (message.kind == CompanionKind::Request) {
            return message.payloadSize == 0;
        }
        if (message.status != CompanionStatus::Ok) {
            return message.payloadSize == 0;
        }
        return capabilitiesPayloadValid(message);
    case CompanionOperation::AppActive:
        if (message.kind == CompanionKind::Request) {
            return message.payloadSize == 0;
        }
        if (message.status == CompanionStatus::Ok) {
            return bundlePayloadValid(message);
        }
        return message.payloadSize == 0;
    case CompanionOperation::AppActivate:
        if (message.kind == CompanionKind::Request) {
            return bundlePayloadValid(message);
        }
        return message.payloadSize == 0;
    case CompanionOperation::AppActiveChanged:
        return message.payloadSize == 0 || bundlePayloadValid(message);
    case CompanionOperation::SystemMetrics: {
        if (message.version < 2)
            return false;
        if (message.kind == CompanionKind::Request || message.status != CompanionStatus::Ok)
            return message.payloadSize == 0;
        CompanionSystemMetrics metrics{};
        return readSystemMetrics(message, metrics);
    }
    case CompanionOperation::AiUsage: {
        if (message.version < 3)
            return false;
        if (message.kind == CompanionKind::Request || message.status != CompanionStatus::Ok)
            return message.payloadSize == 0;
        CompanionAiUsage usage{};
        return readAiUsage(message, usage);
    }
    }
    return false;
}

bool envelopeValid(const CompanionEnvelope& message) noexcept {
    return message.version >= companionProtocolVersion &&
           message.version <= companionLatestProtocolVersion &&
           isKnownKindValue(static_cast<std::uint8_t>(message.kind)) &&
           isKnownOperationValue(static_cast<std::uint8_t>(message.operation)) &&
           isKnownStatusValue(static_cast<std::uint8_t>(message.status)) &&
           operationAllowedForKind(message.kind, message.operation) &&
           headerSemanticsValid(message) && operationPayloadValid(message);
}

bool operationAllowedForKind(CompanionKind kind, CompanionOperation operation) noexcept {
    switch (kind) {
    case CompanionKind::Hello:
    case CompanionKind::HelloAck:
        return operation == CompanionOperation::None;
    case CompanionKind::Request:
        return operation == CompanionOperation::Ping ||
               operation == CompanionOperation::Capabilities ||
               operation == CompanionOperation::AppActive ||
               operation == CompanionOperation::AppActivate ||
               operation == CompanionOperation::SystemMetrics ||
               operation == CompanionOperation::AiUsage;
    case CompanionKind::Response:
        return operation == CompanionOperation::Ping ||
               operation == CompanionOperation::Capabilities ||
               operation == CompanionOperation::AppActive ||
               operation == CompanionOperation::AppActivate ||
               operation == CompanionOperation::SystemMetrics ||
               operation == CompanionOperation::AiUsage;
    case CompanionKind::Event:
        return operation == CompanionOperation::AppActiveChanged;
    }
    return false;
}

bool continuationUtf8(std::uint8_t value) noexcept { return (value & 0xC0U) == 0x80U; }

} // namespace

bool isKnownCompanionKind(std::uint8_t kind) noexcept { return isKnownKindValue(kind); }

bool isKnownCompanionOperation(std::uint8_t operation) noexcept {
    return isKnownOperationValue(operation);
}

bool isKnownCompanionStatus(std::uint8_t status) noexcept { return isKnownStatusValue(status); }

bool isUtf8BundleIdentifier(std::string_view value) noexcept {
    if (value.empty() || value.size() > companionMaxBundleIdSize) {
        return false;
    }
    for (std::size_t index = 0; index < value.size();) {
        const auto lead = static_cast<std::uint8_t>(value[index]);
        std::size_t width = 1;
        std::uint32_t codepoint = 0;
        if (lead == 0) {
            return false;
        }
        if (lead < 0x80U) {
            width = 1;
            codepoint = lead;
        } else if ((lead & 0xE0U) == 0xC0U && lead >= 0xC2U) {
            width = 2;
            codepoint = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            width = 3;
            codepoint = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U && lead <= 0xF4U) {
            width = 4;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (index + width > value.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto cont = static_cast<std::uint8_t>(value[index + offset]);
            if (!continuationUtf8(cont)) {
                return false;
            }
            codepoint = (codepoint << 6U) | (cont & 0x3FU);
        }
        if ((width == 2 && codepoint < 0x80U) || (width == 3 && codepoint < 0x800U) ||
            (width == 4 && codepoint < 0x10000U) ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU) || codepoint > 0x10FFFFU) {
            return false;
        }
        index += width;
    }
    return true;
}

const char* companionCapabilityName(CompanionCapability capability) noexcept {
    switch (capability) {
    case CompanionCapability::AppActive:
        return companionAppActiveCapabilityId;
    case CompanionCapability::AppActivate:
        return companionAppActivateCapabilityId;
    case CompanionCapability::AppActiveEvents:
        return companionAppActiveEventsCapabilityId;
    case CompanionCapability::SystemMetrics:
        return companionSystemMetricsCapabilityId;
    case CompanionCapability::AiUsage:
        return companionAiUsageCapabilityId;
    }
    return nullptr;
}

std::optional<CompanionEncodedMessage> encodeCompanionMessage(const CompanionEnvelope& message) {
    if (!isKnownKindValue(static_cast<std::uint8_t>(message.kind)) ||
        !isKnownOperationValue(static_cast<std::uint8_t>(message.operation)) ||
        !isKnownStatusValue(static_cast<std::uint8_t>(message.status)) ||
        message.payloadSize > companionMaxPayloadSize || !envelopeValid(message)) {
        return std::nullopt;
    }
    CompanionEncodedMessage encoded{};
    encoded.size = static_cast<std::uint16_t>(companionEnvelopeSize + message.payloadSize);
    encoded.bytes[0] = message.version;
    encoded.bytes[1] = static_cast<std::uint8_t>(message.kind);
    encoded.bytes[2] = static_cast<std::uint8_t>(message.session & 0xFFU);
    encoded.bytes[3] = static_cast<std::uint8_t>((message.session >> 8U) & 0xFFU);
    encoded.bytes[4] = message.requestId;
    encoded.bytes[5] = static_cast<std::uint8_t>(message.operation);
    encoded.bytes[6] = static_cast<std::uint8_t>(message.status);
    encoded.bytes[7] = message.payloadSize;
    if (message.payloadSize > 0) {
        std::memcpy(encoded.bytes.data() + companionEnvelopeSize, message.payload.data(),
                    message.payloadSize);
    }
    return encoded;
}

std::optional<CompanionEnvelope> decodeCompanionMessage(const std::uint8_t* data,
                                                        std::size_t size) {
    if (data == nullptr || size < companionEnvelopeSize || size > companionMaxMessageSize) {
        return std::nullopt;
    }
    const auto payloadSize = data[7];
    if (size != companionEnvelopeSize + payloadSize) {
        return std::nullopt;
    }
    if ((data[0] < companionProtocolVersion || data[0] > companionLatestProtocolVersion) ||
        !isKnownKindValue(data[1]) || !isKnownOperationValue(data[5]) ||
        !isKnownStatusValue(data[6])) {
        return std::nullopt;
    }
    CompanionEnvelope message{};
    message.version = data[0];
    message.kind = static_cast<CompanionKind>(data[1]);
    message.session =
        static_cast<std::uint16_t>(data[2] | (static_cast<std::uint16_t>(data[3]) << 8U));
    message.requestId = data[4];
    message.operation = static_cast<CompanionOperation>(data[5]);
    message.status = static_cast<CompanionStatus>(data[6]);
    message.payloadSize = payloadSize;
    if (payloadSize > 0) {
        std::memcpy(message.payload.data(), data + companionEnvelopeSize, payloadSize);
    }
    if (!envelopeValid(message)) {
        return std::nullopt;
    }
    return message;
}

CompanionEnvelope makeHello(const std::uint8_t* versions, std::uint8_t count) {
    CompanionEnvelope message{};
    message.kind = CompanionKind::Hello;
    if (versions == nullptr || count == 0 || count > companionMaxSupportedVersions) {
        return message;
    }
    message.payload[0] = count;
    std::memcpy(message.payload.data() + 1, versions, count);
    message.payloadSize = static_cast<std::uint8_t>(count + 1);
    return message;
}

CompanionEnvelope makeHelloAck(std::uint16_t session, std::uint8_t protocol) {
    CompanionEnvelope message{};
    message.kind = CompanionKind::HelloAck;
    message.session = session;
    message.payload[0] = protocol;
    message.payloadSize = 1;
    return message;
}

CompanionEnvelope makeRequest(std::uint16_t session, std::uint8_t requestId,
                              CompanionOperation operation) {
    CompanionEnvelope message{};
    message.kind = CompanionKind::Request;
    message.session = session;
    message.requestId = requestId;
    message.operation = operation;
    return message;
}

CompanionEnvelope makeResponse(std::uint16_t session, std::uint8_t requestId,
                               CompanionOperation operation, CompanionStatus status) {
    CompanionEnvelope message{};
    message.kind = CompanionKind::Response;
    message.session = session;
    message.requestId = requestId;
    message.operation = operation;
    message.status = status;
    return message;
}

CompanionEnvelope makeEvent(std::uint16_t session, CompanionOperation operation) {
    CompanionEnvelope message{};
    message.kind = CompanionKind::Event;
    message.session = session;
    message.operation = operation;
    return message;
}

bool setPingToken(CompanionEnvelope& message,
                  const std::array<std::uint8_t, companionPingTokenSize>& token) {
    if (message.operation != CompanionOperation::Ping) {
        return false;
    }
    std::memcpy(message.payload.data(), token.data(), token.size());
    message.payloadSize = static_cast<std::uint8_t>(token.size());
    return true;
}

bool readPingToken(const CompanionEnvelope& message,
                   std::array<std::uint8_t, companionPingTokenSize>& token) {
    if (message.operation != CompanionOperation::Ping ||
        message.payloadSize != companionPingTokenSize) {
        return false;
    }
    std::memcpy(token.data(), message.payload.data(), token.size());
    return true;
}

bool setCapabilityList(CompanionEnvelope& message, const CompanionCapability* capabilities,
                       std::uint8_t count) {
    if (message.operation != CompanionOperation::Capabilities || capabilities == nullptr ||
        count == 0 || count > companionMaxCapabilities) {
        return false;
    }
    message.payload[0] = count;
    for (std::uint8_t index = 0; index < count; ++index) {
        if (capabilities[index] == CompanionCapability::SystemMetrics && message.version < 2)
            return false;
        if (capabilities[index] == CompanionCapability::AiUsage && message.version < 3)
            return false;
        message.payload[index + 1] = static_cast<std::uint8_t>(capabilities[index]);
    }
    message.payloadSize = static_cast<std::uint8_t>(count + 1);
    return true;
}

bool readCapabilityList(const CompanionEnvelope& message, CompanionCapability* capabilities,
                        std::uint8_t& count, std::uint8_t capacity) {
    count = 0;
    if (message.operation != CompanionOperation::Capabilities || capabilities == nullptr ||
        message.payloadSize < 2 || message.payload[0] == 0 ||
        message.payloadSize != static_cast<std::uint8_t>(message.payload[0] + 1) ||
        message.payload[0] > capacity || message.payload[0] > companionMaxCapabilities) {
        return false;
    }
    for (std::uint8_t index = 0; index < message.payload[0]; ++index) {
        const auto value = message.payload[index + 1];
        const auto maximum = message.version >= 3   ? CompanionCapability::AiUsage
                             : message.version >= 2 ? CompanionCapability::SystemMetrics
                                                    : CompanionCapability::AppActiveEvents;
        if (value < static_cast<std::uint8_t>(CompanionCapability::AppActive) ||
            value > static_cast<std::uint8_t>(maximum)) {
            count = 0;
            return false;
        }
        capabilities[index] = static_cast<CompanionCapability>(value);
    }
    count = message.payload[0];
    return true;
}

bool setBundleIdentifier(CompanionEnvelope& message, std::string_view bundleId) {
    if ((message.operation != CompanionOperation::AppActive &&
         message.operation != CompanionOperation::AppActivate &&
         message.operation != CompanionOperation::AppActiveChanged) ||
        !isUtf8BundleIdentifier(bundleId)) {
        return false;
    }
    message.payload[0] = static_cast<std::uint8_t>(bundleId.size());
    std::memcpy(message.payload.data() + 1, bundleId.data(), bundleId.size());
    message.payloadSize = static_cast<std::uint8_t>(bundleId.size() + 1);
    return true;
}

bool readBundleIdentifier(const CompanionEnvelope& message, char* destination, std::size_t capacity,
                          std::uint8_t& length) {
    length = 0;
    if (destination == nullptr || capacity == 0 || message.payloadSize < 2 ||
        message.payload[0] == 0 ||
        message.payloadSize != static_cast<std::uint8_t>(message.payload[0] + 1) ||
        message.payload[0] >= capacity) {
        return false;
    }
    const auto view = std::string_view(reinterpret_cast<const char*>(message.payload.data() + 1),
                                       message.payload[0]);
    if (!isUtf8BundleIdentifier(view)) {
        return false;
    }
    std::memcpy(destination, view.data(), view.size());
    destination[view.size()] = '\0';
    length = static_cast<std::uint8_t>(view.size());
    return true;
}

namespace {
void write32(std::uint8_t* bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint32_t read32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}
bool validMetrics(const CompanionSystemMetrics& value) {
    return (value.validity & ~std::uint16_t{0x7f}) == 0 &&
           (!(value.validity & 1U) || value.cpuPercent <= 100) &&
           (!(value.validity & 2U) ||
            (value.memoryTotalMiB > 0 && value.memoryUsedMiB <= value.memoryTotalMiB)) &&
           (!(value.validity & 4U) || (value.memoryPressure >= 1 && value.memoryPressure <= 3)) &&
           (!(value.validity & 8U) || value.diskUsedPercent <= 100) &&
           (!(value.validity & 16U) || value.batteryPercent <= 100) &&
           (!(value.validity & 64U) || (value.thermalState >= 1 && value.thermalState <= 4));
}
} // namespace

bool setSystemMetrics(CompanionEnvelope& message, const CompanionSystemMetrics& metrics) {
    if (message.operation != CompanionOperation::SystemMetrics ||
        message.kind != CompanionKind::Response || message.status != CompanionStatus::Ok ||
        message.version < 2 || !validMetrics(metrics))
        return false;
    auto* p = message.payload.data();
    p[0] = 1;
    p[1] = static_cast<std::uint8_t>(metrics.validity);
    p[2] = static_cast<std::uint8_t>(metrics.validity >> 8U);
    p[3] = metrics.cpuPercent;
    write32(p + 4, metrics.memoryUsedMiB);
    write32(p + 8, metrics.memoryTotalMiB);
    p[12] = metrics.memoryPressure;
    p[13] = metrics.diskUsedPercent;
    p[14] = metrics.batteryPercent;
    p[15] = metrics.thermalState;
    write32(p + 16, metrics.downloadKiBps);
    write32(p + 20, metrics.uploadKiBps);
    message.payloadSize = companionMetricsPayloadSize;
    return true;
}

bool readSystemMetrics(const CompanionEnvelope& message, CompanionSystemMetrics& metrics) {
    if (message.operation != CompanionOperation::SystemMetrics ||
        message.kind != CompanionKind::Response || message.status != CompanionStatus::Ok ||
        message.version < 2 || message.payloadSize != companionMetricsPayloadSize ||
        message.payload[0] != 1)
        return false;
    const auto* p = message.payload.data();
    CompanionSystemMetrics result{};
    result.validity = static_cast<std::uint16_t>(p[1] | (std::uint16_t(p[2]) << 8U));
    result.cpuPercent = p[3];
    result.memoryUsedMiB = read32(p + 4);
    result.memoryTotalMiB = read32(p + 8);
    result.memoryPressure = p[12];
    result.diskUsedPercent = p[13];
    result.batteryPercent = p[14];
    result.thermalState = p[15];
    result.downloadKiBps = read32(p + 16);
    result.uploadKiBps = read32(p + 20);
    if (!validMetrics(result))
        return false;
    metrics = result;
    return true;
}

namespace {
bool validAiMetric(const AiUsageMetric& metric) {
    return static_cast<std::uint8_t>(metric.kind) >= 1 &&
           static_cast<std::uint8_t>(metric.kind) <= 4 &&
           static_cast<std::uint8_t>(metric.unit) >= 1 &&
           static_cast<std::uint8_t>(metric.unit) <= 3 && metric.remainingPercent <= 100 &&
           (metric.limit == 0 || (metric.used <= metric.limit && metric.remaining <= metric.limit));
}
// Schema 3 (protocol v5) adds Claude and its Pro/Max plans.
bool validAiProvider(const AiUsageProvider& provider, std::uint8_t schema) {
    if (static_cast<std::uint8_t>(provider.provider) < 1 ||
        static_cast<std::uint8_t>(provider.provider) > (schema >= 3 ? 3 : 2) ||
        static_cast<std::uint8_t>(provider.plan) > (schema >= 3 ? 5 : 3) ||
        static_cast<std::uint8_t>(provider.freshness) < 1 ||
        static_cast<std::uint8_t>(provider.freshness) > 2 || provider.metricCount == 0 ||
        provider.metricCount > 2)
        return false;
    for (std::uint8_t i = 0; i < provider.metricCount; ++i)
        if (!validAiMetric(provider.metrics[i]))
            return false;
    return true;
}
} // namespace

bool setAiUsage(CompanionEnvelope& message, const CompanionAiUsage& usage) {
    if (message.operation != CompanionOperation::AiUsage ||
        message.kind != CompanionKind::Response || message.status != CompanionStatus::Ok ||
        message.version < 3 || usage.providerCount > 2 ||
        (usage.state != AiUsageState::Discovering && usage.state != AiUsageState::Ready))
        return false;
    std::size_t pos = 0;
    auto* p = message.payload.data();
    const auto schema = static_cast<std::uint8_t>(message.version - 2);
    p[pos++] = schema;
    p[pos++] = static_cast<std::uint8_t>(usage.state);
    p[pos++] = usage.providerCount;
    write32(p + pos, usage.generation);
    pos += 4;
    for (std::uint8_t i = 0; i < usage.providerCount; ++i) {
        const auto& provider = usage.providers[i];
        if (!validAiProvider(provider, schema) ||
            (i == 1 && provider.provider == usage.providers[0].provider))
            return false;
        p[pos++] = static_cast<std::uint8_t>(provider.provider);
        p[pos++] = static_cast<std::uint8_t>(provider.plan);
        p[pos++] = static_cast<std::uint8_t>(provider.freshness);
        p[pos++] = provider.metricCount;
        for (std::uint8_t j = 0; j < provider.metricCount; ++j) {
            if (pos + 23 > companionMaxPayloadSize)
                return false;
            const auto& metric = provider.metrics[j];
            p[pos++] = static_cast<std::uint8_t>(metric.kind);
            p[pos++] = static_cast<std::uint8_t>(metric.unit);
            write32(p + pos, metric.used);
            pos += 4;
            write32(p + pos, metric.limit);
            pos += 4;
            write32(p + pos, metric.remaining);
            pos += 4;
            p[pos++] = metric.remainingPercent;
            write32(p + pos, metric.resetAt);
            pos += 4;
            write32(p + pos, metric.resetRemainingSeconds);
            pos += 4;
        }
        if (message.version >= 4) {
            const auto& resets = provider.resetCredits;
            if (resets.known &&
                (provider.provider != AiProvider::Codex || provider.plan != AiPlan::Plus ||
                 resets.creditCount > 4 || resets.creditCount > resets.availableCount))
                return false;
            if (pos + (resets.known ? 3U : 1U) > companionMaxPayloadSize)
                return false;
            p[pos++] = resets.known ? 1 : 0;
            if (resets.known) {
                p[pos++] = resets.availableCount;
                p[pos++] = resets.creditCount;
                for (std::uint8_t j = 0; j < resets.creditCount; ++j) {
                    const auto& credit = resets.credits[j];
                    const auto length = strnlen(credit.title.data(), credit.title.size());
                    if (length == 0 || length > 24 ||
                        !isUtf8BundleIdentifier({credit.title.data(), length}) ||
                        pos + 1 + length + 8 > companionMaxPayloadSize)
                        return false;
                    p[pos++] = static_cast<std::uint8_t>(length);
                    std::memcpy(p + pos, credit.title.data(), length);
                    pos += length;
                    write32(p + pos, credit.expiresAt);
                    pos += 4;
                    write32(p + pos, credit.expiresRemainingSeconds);
                    pos += 4;
                }
            }
        }
    }
    message.payloadSize = static_cast<std::uint8_t>(pos);
    return true;
}

bool readAiUsage(const CompanionEnvelope& message, CompanionAiUsage& usage) {
    if (message.operation != CompanionOperation::AiUsage ||
        message.kind != CompanionKind::Response || message.status != CompanionStatus::Ok ||
        message.version < 3 || message.payloadSize < 7 ||
        message.payload[0] != message.version - 2 || message.payload[1] < 1 ||
        message.payload[1] > 2 || message.payload[2] > 2)
        return false;
    CompanionAiUsage result{};
    const auto* p = message.payload.data();
    result.schemaVersion = p[0];
    result.state = static_cast<AiUsageState>(p[1]);
    result.providerCount = p[2];
    result.generation = read32(p + 3);
    std::size_t pos = 7;
    for (std::uint8_t i = 0; i < result.providerCount; ++i) {
        if (pos + 4 > message.payloadSize)
            return false;
        auto& provider = result.providers[i];
        provider.provider = static_cast<AiProvider>(p[pos++]);
        provider.plan = static_cast<AiPlan>(p[pos++]);
        provider.freshness = static_cast<AiFreshness>(p[pos++]);
        provider.metricCount = p[pos++];
        if (provider.metricCount == 0 || provider.metricCount > 2 ||
            (i == 1 && provider.provider == result.providers[0].provider))
            return false;
        for (std::uint8_t j = 0; j < provider.metricCount; ++j) {
            if (pos + 23 > message.payloadSize)
                return false;
            auto& metric = provider.metrics[j];
            metric.kind = static_cast<AiMetricKind>(p[pos++]);
            metric.unit = static_cast<AiMetricUnit>(p[pos++]);
            metric.used = read32(p + pos);
            pos += 4;
            metric.limit = read32(p + pos);
            pos += 4;
            metric.remaining = read32(p + pos);
            pos += 4;
            metric.remainingPercent = p[pos++];
            metric.resetAt = read32(p + pos);
            pos += 4;
            metric.resetRemainingSeconds = read32(p + pos);
            pos += 4;
        }
        if (!validAiProvider(provider, result.schemaVersion))
            return false;
        if (message.version >= 4) {
            if (pos >= message.payloadSize || p[pos] > 1)
                return false;
            auto& resets = provider.resetCredits;
            resets.known = p[pos++] == 1;
            if (resets.known) {
                if (provider.provider != AiProvider::Codex || provider.plan != AiPlan::Plus ||
                    pos + 2 > message.payloadSize || p[pos + 1] > 4 || p[pos + 1] > p[pos])
                    return false;
                resets.availableCount = p[pos++];
                resets.creditCount = p[pos++];
                for (std::uint8_t j = 0; j < resets.creditCount; ++j) {
                    if (pos >= message.payloadSize || p[pos] == 0 || p[pos] > 24)
                        return false;
                    const auto length = p[pos++];
                    if (pos + length + 8 > message.payloadSize ||
                        !isUtf8BundleIdentifier({reinterpret_cast<const char*>(p + pos), length}))
                        return false;
                    auto& credit = resets.credits[j];
                    std::memcpy(credit.title.data(), p + pos, length);
                    pos += length;
                    credit.expiresAt = read32(p + pos);
                    pos += 4;
                    credit.expiresRemainingSeconds = read32(p + pos);
                    pos += 4;
                }
            }
        }
    }
    if (pos != message.payloadSize)
        return false;
    usage = result;
    return true;
}

} // namespace cardputer_hub::connectivity
