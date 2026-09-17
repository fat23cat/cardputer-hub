#include "connectivity/companion/companion_protocol.h"

#include <cstring>

namespace cardputer_hub::connectivity {
namespace {

bool isKnownKindValue(std::uint8_t kind) noexcept {
    return kind >= static_cast<std::uint8_t>(CompanionKind::Hello) &&
           kind <= static_cast<std::uint8_t>(CompanionKind::Event);
}

bool isKnownOperationValue(std::uint8_t operation) noexcept {
    return operation <= static_cast<std::uint8_t>(CompanionOperation::AppActiveChanged);
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
               message.payload[0] == companionProtocolVersion;
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
    }
    return false;
}

bool envelopeValid(const CompanionEnvelope& message) noexcept {
    return isKnownKindValue(static_cast<std::uint8_t>(message.kind)) &&
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
               operation == CompanionOperation::AppActivate;
    case CompanionKind::Response:
        return operation == CompanionOperation::Ping ||
               operation == CompanionOperation::Capabilities ||
               operation == CompanionOperation::AppActive ||
               operation == CompanionOperation::AppActivate;
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
    }
    return nullptr;
}

std::optional<CompanionEncodedMessage> encodeCompanionMessage(const CompanionEnvelope& message) {
    if (message.version != companionProtocolVersion ||
        !isKnownKindValue(static_cast<std::uint8_t>(message.kind)) ||
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
    if (data[0] != companionProtocolVersion || !isKnownKindValue(data[1]) ||
        !isKnownOperationValue(data[5]) || !isKnownStatusValue(data[6])) {
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
        if (value < static_cast<std::uint8_t>(CompanionCapability::AppActive) ||
            value > static_cast<std::uint8_t>(CompanionCapability::AppActiveEvents)) {
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

} // namespace cardputer_hub::connectivity
