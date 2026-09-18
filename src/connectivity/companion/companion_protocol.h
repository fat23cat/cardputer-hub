#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cardputer_hub::connectivity {

inline constexpr std::uint8_t companionProtocolVersion = 1;
inline constexpr std::size_t companionMaxMessageSize = 256;
inline constexpr std::size_t companionEnvelopeSize = 8;
inline constexpr std::size_t companionMaxPayloadSize =
    companionMaxMessageSize - companionEnvelopeSize;
inline constexpr std::size_t companionMaxBundleIdSize = 128;
inline constexpr std::size_t companionPingTokenSize = 4;
inline constexpr std::size_t companionMaxOutstandingRequests = 4;
inline constexpr std::uint8_t companionMaxSupportedVersions = 4;
inline constexpr std::uint8_t companionMaxCapabilities = 8;

inline constexpr char companionServiceUuid[] = "07B23AB1-3938-418A-8E16-0AEC1CAA517F";
inline constexpr char companionHostToDeviceUuid[] = "792B8431-054D-4758-B198-3EE728EA6FE1";
inline constexpr char companionDeviceToHostUuid[] = "BE3869D8-8F00-4D4B-A4CA-954A2D5B3EE1";

inline constexpr std::array<std::uint8_t, 16> companionServiceUuidBytes{
    0x7F, 0x51, 0xAA, 0x1C, 0xEC, 0x0A, 0x16, 0x8E, 0x8A, 0x41, 0x38, 0x39, 0xB1, 0x3A, 0xB2, 0x07};
inline constexpr std::array<std::uint8_t, 16> companionHostToDeviceUuidBytes{
    0xE1, 0x6F, 0xEA, 0x28, 0xE7, 0x3E, 0x98, 0xB1, 0x58, 0x47, 0x4D, 0x05, 0x31, 0x84, 0x2B, 0x79};
inline constexpr std::array<std::uint8_t, 16> companionDeviceToHostUuidBytes{
    0xE1, 0x3E, 0x5B, 0x2D, 0x4A, 0x95, 0xCA, 0xA4, 0x4B, 0x4D, 0x00, 0x8F, 0xD8, 0x69, 0x38, 0xBE};

inline constexpr char companionCapabilityId[] = "COMPANION";
inline constexpr char companionAppActiveCapabilityId[] = "APP_ACTIVE";
inline constexpr char companionAppActivateCapabilityId[] = "APP_ACTIVATE";
inline constexpr char companionAppActiveEventsCapabilityId[] = "APP_ACTIVE_EVENTS";

enum class CompanionKind : std::uint8_t {
    Hello = 1,
    HelloAck = 2,
    Request = 3,
    Response = 4,
    Event = 5,
};

enum class CompanionOperation : std::uint8_t {
    None = 0,
    Ping = 1,
    Capabilities = 2,
    AppActive = 3,
    AppActivate = 4,
    AppActiveChanged = 5,
};

enum class CompanionStatus : std::uint8_t {
    Ok = 0,
    NotAvailable = 1,
    NotFound = 2,
    Unsupported = 3,
    Malformed = 4,
};

enum class CompanionCapability : std::uint8_t {
    AppActive = 1,
    AppActivate = 2,
    AppActiveEvents = 3,
};

struct CompanionEncodedMessage {
    std::array<std::uint8_t, companionMaxMessageSize> bytes{};
    std::uint16_t size = 0;
};

struct CompanionEnvelope {
    std::uint8_t version = companionProtocolVersion;
    CompanionKind kind = CompanionKind::Request;
    std::uint16_t session = 0;
    std::uint8_t requestId = 0;
    CompanionOperation operation = CompanionOperation::None;
    CompanionStatus status = CompanionStatus::Ok;
    std::array<std::uint8_t, companionMaxPayloadSize> payload{};
    std::uint8_t payloadSize = 0;
};

bool isKnownCompanionKind(std::uint8_t kind) noexcept;
bool isKnownCompanionOperation(std::uint8_t operation) noexcept;
bool isKnownCompanionStatus(std::uint8_t status) noexcept;
bool isUtf8BundleIdentifier(std::string_view value) noexcept;
const char* companionCapabilityName(CompanionCapability capability) noexcept;

std::optional<CompanionEncodedMessage> encodeCompanionMessage(const CompanionEnvelope& message);
std::optional<CompanionEnvelope> decodeCompanionMessage(const std::uint8_t* data, std::size_t size);

CompanionEnvelope makeHello(const std::uint8_t* versions, std::uint8_t count);
CompanionEnvelope makeHelloAck(std::uint16_t session, std::uint8_t protocol);
CompanionEnvelope makeRequest(std::uint16_t session, std::uint8_t requestId,
                              CompanionOperation operation);
CompanionEnvelope makeResponse(std::uint16_t session, std::uint8_t requestId,
                               CompanionOperation operation, CompanionStatus status);
CompanionEnvelope makeEvent(std::uint16_t session, CompanionOperation operation);

bool setPingToken(CompanionEnvelope& message,
                  const std::array<std::uint8_t, companionPingTokenSize>& token);
bool readPingToken(const CompanionEnvelope& message,
                   std::array<std::uint8_t, companionPingTokenSize>& token);
bool setCapabilityList(CompanionEnvelope& message, const CompanionCapability* capabilities,
                       std::uint8_t count);
bool readCapabilityList(const CompanionEnvelope& message, CompanionCapability* capabilities,
                        std::uint8_t& count, std::uint8_t capacity);
bool setBundleIdentifier(CompanionEnvelope& message, std::string_view bundleId);
bool readBundleIdentifier(const CompanionEnvelope& message, char* destination, std::size_t capacity,
                          std::uint8_t& length);

} // namespace cardputer_hub::connectivity
