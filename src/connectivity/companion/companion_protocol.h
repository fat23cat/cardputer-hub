#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cardputer_hub::connectivity {

// Firmware and Companion are built from the same commit: there is one wire
// format, and HELLO carries the generated protocol fingerprint instead of a
// version list. Byte 0 of every envelope is this marker; frames from a peer
// built before plan 043 start with a version (1-6) instead.
inline constexpr std::uint8_t companionFrameMarker = 0xC7;
inline constexpr std::uint8_t companionLegacyMaxVersion = 6;
inline constexpr std::size_t companionFingerprintSize = 8;
inline constexpr std::size_t companionMaxBuildIdSize = 24;
inline constexpr std::size_t companionMetricsPayloadSize = 26;
inline constexpr std::size_t companionMaxProcessNameSize = 20;
inline constexpr std::size_t companionMaxPeripheralNameSize = 16;
inline constexpr std::uint8_t companionMaxDetailProcesses = 4;
inline constexpr std::size_t companionMaxMessageSize = 256;
inline constexpr std::size_t companionEnvelopeSize = 8;
inline constexpr std::size_t companionMaxPayloadSize =
    companionMaxMessageSize - companionEnvelopeSize;
inline constexpr std::size_t companionMaxBundleIdSize = 128;
inline constexpr std::size_t companionPingTokenSize = 4;
inline constexpr std::size_t companionMaxOutstandingRequests = 4;
// Inventory transfers (Mac requests, Cardputer responses). A record is moved in
// bounded chunks; it is committed only after the last one arrives complete.
inline constexpr std::size_t companionInventoryIdSize = 16;
inline constexpr std::size_t companionInventoryMaxRecordBytes = 4096;
inline constexpr std::size_t companionInventoryMaxNameBytes = 128;
inline constexpr std::size_t companionInventoryGetHeaderSize = 8;
inline constexpr std::size_t companionInventoryGetChunkSize =
    companionMaxPayloadSize - companionInventoryGetHeaderSize;
inline constexpr std::size_t companionInventoryPutHeaderSize = 24;
inline constexpr std::size_t companionInventoryPutChunkSize =
    companionMaxPayloadSize - companionInventoryPutHeaderSize;
inline constexpr std::size_t companionInventoryListHeaderSize = 5;

inline constexpr char companionServiceUuid[] = "07B23AB1-3938-418A-8E16-0AEC1CAA517F";
inline constexpr char companionHostToDeviceUuid[] = "792B8431-054D-4758-B198-3EE728EA6FE1";
inline constexpr char companionDeviceToHostUuid[] = "BE3869D8-8F00-4D4B-A4CA-954A2D5B3EE1";

inline constexpr std::array<std::uint8_t, 16> companionServiceUuidBytes{
    0x7F, 0x51, 0xAA, 0x1C, 0xEC, 0x0A, 0x16, 0x8E, 0x8A, 0x41, 0x38, 0x39, 0xB1, 0x3A, 0xB2, 0x07};
inline constexpr std::array<std::uint8_t, 16> companionHostToDeviceUuidBytes{
    0xE1, 0x6F, 0xEA, 0x28, 0xE7, 0x3E, 0x98, 0xB1, 0x58, 0x47, 0x4D, 0x05, 0x31, 0x84, 0x2B, 0x79};
inline constexpr std::array<std::uint8_t, 16> companionDeviceToHostUuidBytes{
    0xE1, 0x3E, 0x5B, 0x2D, 0x4A, 0x95, 0xCA, 0xA4, 0x4B, 0x4D, 0x00, 0x8F, 0xD8, 0x69, 0x38, 0xBE};

// Published while a Companion session is Ready; every Companion Mini App needs only this.
inline constexpr char companionCapabilityId[] = "COMPANION";

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
    // 2 was CAPABILITIES before plan 043.
    AppActive = 3,
    AppActivate = 4,
    AppActiveChanged = 5,
    SystemMetrics = 6,
    AiUsage = 7,
    SystemDetails = 8,
    InventoryList = 9,
    InventoryGet = 10,
    InventoryPut = 11,
    InventoryDelete = 12,
};

enum class CompanionStatus : std::uint8_t {
    Ok = 0,
    NotAvailable = 1,
    NotFound = 2,
    Unsupported = 3,
    Malformed = 4,
    // An edit was based on an older revision; the payload is the current one.
    Conflict = 5,
    // The record (stored or submitted) is not valid, or a transfer is out of order.
    Rejected = 6,
    StorageError = 7,
};

using CompanionInventoryId = std::array<std::uint8_t, companionInventoryIdSize>;

struct CompanionInventoryListEntry {
    CompanionInventoryId id{};
    // False for a stored file that is not a valid record: no revision, no name.
    bool valid = true;
    std::uint32_t revision = 0;
    std::string_view name;
};

struct CompanionInventoryChunk {
    CompanionInventoryId id{};
    // GET: the record's revision. PUT: the revision the edit was based on.
    std::uint32_t revision = 0;
    std::uint16_t total = 0;
    std::uint16_t offset = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

enum class AiUsageState : std::uint8_t { Discovering = 1, Ready = 2 };
enum class AiProvider : std::uint8_t { Codex = 1, Cursor = 2, Claude = 3 };
enum class AiPlan : std::uint8_t {
    Unknown = 0,
    Plus = 1,
    Business = 2,
    Enterprise = 3,
    Pro = 4,
    Max = 5
};
enum class AiFreshness : std::uint8_t { Fresh = 1, Stale = 2 };
enum class AiMetricKind : std::uint8_t { FiveHour = 1, Week = 2, Credits = 3, Money = 4 };
enum class AiMetricUnit : std::uint8_t { Percent = 1, Credits = 2, Cents = 3 };

struct AiUsageMetric {
    AiMetricKind kind = AiMetricKind::FiveHour;
    AiMetricUnit unit = AiMetricUnit::Percent;
    std::uint32_t used = 0;
    std::uint32_t limit = 0;
    std::uint32_t remaining = 0;
    std::uint8_t remainingPercent = 0;
    std::uint32_t resetAt = 0;
    std::uint32_t resetRemainingSeconds = 0;
};

struct AiResetCredit {
    std::array<char, 25> title{};
    std::uint32_t expiresAt = 0;
    std::uint32_t expiresRemainingSeconds = 0;
};

struct AiResetCredits {
    bool known = false;
    std::uint8_t availableCount = 0;
    std::uint8_t creditCount = 0;
    std::array<AiResetCredit, 4> credits{};
};

struct AiUsageProvider {
    AiProvider provider = AiProvider::Codex;
    AiPlan plan = AiPlan::Unknown;
    AiFreshness freshness = AiFreshness::Fresh;
    std::uint8_t metricCount = 0;
    std::array<AiUsageMetric, 2> metrics{};
    AiResetCredits resetCredits{};
};

struct CompanionAiUsage {
    std::uint32_t generation = 0;
    AiUsageState state = AiUsageState::Discovering;
    std::uint8_t providerCount = 0;
    std::array<AiUsageProvider, 2> providers{};
};

struct CompanionSystemMetrics {
    std::uint16_t validity = 0;
    std::uint8_t cpuPercent = 0;
    std::uint32_t memoryUsedMiB = 0;
    std::uint32_t memoryTotalMiB = 0;
    std::uint8_t memoryPressure = 0;
    std::uint8_t diskUsedPercent = 0;
    std::uint8_t batteryPercent = 0;
    std::uint8_t thermalState = 0;
    std::uint32_t downloadKiBps = 0;
    std::uint32_t uploadKiBps = 0;
    // Validity bit 7 (1 battery, 2 AC charging, 3 AC not charging) and bit 8
    // (minutes to full while charging, to empty on battery).
    std::uint8_t powerSource = 0;
    std::uint16_t batteryMinutes = 0;
};

enum class SystemDetailsGroup : std::uint8_t { Cpu = 1, Power = 2, Network = 3, Memory = 4 };

struct SystemDetailsProcess {
    std::uint8_t percent = 0;
    std::array<char, companionMaxProcessNameSize + 1> name{};
};

// One SYSTEM_DETAILS group; only the fields of `group` are meaningful, and only
// where their validity bit is set.
struct CompanionSystemDetails {
    SystemDetailsGroup group = SystemDetailsGroup::Cpu;
    std::uint16_t validity = 0;
    std::uint8_t performancePercent = 0;
    std::uint8_t efficiencyPercent = 0;
    std::uint8_t gpuPercent = 0;
    std::uint8_t processCount = 0;
    std::array<SystemDetailsProcess, companionMaxDetailProcesses> processes{};
    std::uint16_t systemDrawDeciwatts = 0;
    std::uint8_t adapterWatts = 0;
    std::uint8_t healthPercent = 0;
    std::uint16_t cycleCount = 0;
    std::uint8_t peripheralPercent = 0;
    std::array<char, companionMaxPeripheralNameSize + 1> peripheralName{};
    std::uint16_t internetRttMs = 0;
    std::uint16_t routerRttMs = 0;
    std::int8_t wifiRssiDbm = 0;
    std::uint16_t wifiLinkMbps = 0;
    std::uint32_t appMiB = 0;
    std::uint32_t wiredMiB = 0;
    std::uint32_t compressedMiB = 0;
    std::uint32_t swapUsedMiB = 0;
    std::uint16_t ssdFreeGB = 0;
    std::uint16_t ssdTotalGB = 0;
    std::uint32_t diskReadKiBps = 0;
    std::uint32_t diskWriteKiBps = 0;
};

struct CompanionEncodedMessage {
    std::array<std::uint8_t, companionMaxMessageSize> bytes{};
    std::uint16_t size = 0;
};

// HELLO (Mac) and HELLO_ACK (Cardputer) payload: the sender's protocol
// fingerprint and its build id, for example "2026-09-29 abc1234".
struct CompanionHello {
    std::array<std::uint8_t, companionFingerprintSize> fingerprint{};
    std::array<char, companionMaxBuildIdSize + 1> buildId{};
};

struct CompanionEnvelope {
    CompanionKind kind = CompanionKind::Request;
    std::uint16_t session = 0;
    std::uint8_t requestId = 0;
    CompanionOperation operation = CompanionOperation::None;
    CompanionStatus status = CompanionStatus::Ok;
    std::array<std::uint8_t, companionMaxPayloadSize> payload{};
    std::uint8_t payloadSize = 0;
};

// Telemetry snapshots change no session state, so a malformed response fails
// only its own request; any other malformed message is a protocol error.
bool companionResponseFailureIsIsolated(CompanionOperation operation) noexcept;
bool isKnownCompanionKind(std::uint8_t kind) noexcept;
bool isKnownCompanionOperation(std::uint8_t operation) noexcept;
bool isKnownCompanionStatus(std::uint8_t status) noexcept;
bool isUtf8BundleIdentifier(std::string_view value) noexcept;
// A frame from a Companion or firmware built before plan 043 (version byte 1-6).
bool isLegacyCompanionFrame(const std::uint8_t* data, std::size_t size) noexcept;
bool isLegacyCompanionHello(const std::uint8_t* data, std::size_t size) noexcept;

std::optional<CompanionEncodedMessage> encodeCompanionMessage(const CompanionEnvelope& message);
std::optional<CompanionEnvelope> decodeCompanionMessage(const std::uint8_t* data, std::size_t size);

// Build ids are printable ASCII, 1-24 bytes; anything else makes these fail.
std::optional<CompanionEnvelope> makeHello(const CompanionHello& hello);
// Accepted: a new non-zero session and status OK. Mismatch: session 0 and
// status UNSUPPORTED. Both carry the firmware's fingerprint and build id.
std::optional<CompanionEnvelope> makeHelloAck(std::uint16_t session, CompanionStatus status,
                                              const CompanionHello& hello);
bool readHello(const CompanionEnvelope& message, CompanionHello& hello);
CompanionEnvelope makeRequest(std::uint16_t session, std::uint8_t requestId,
                              CompanionOperation operation);
CompanionEnvelope makeResponse(std::uint16_t session, std::uint8_t requestId,
                               CompanionOperation operation, CompanionStatus status);
CompanionEnvelope makeEvent(std::uint16_t session, CompanionOperation operation);

bool setPingToken(CompanionEnvelope& message,
                  const std::array<std::uint8_t, companionPingTokenSize>& token);
bool readPingToken(const CompanionEnvelope& message,
                   std::array<std::uint8_t, companionPingTokenSize>& token);
bool setBundleIdentifier(CompanionEnvelope& message, std::string_view bundleId);
bool readBundleIdentifier(const CompanionEnvelope& message, char* destination, std::size_t capacity,
                          std::uint8_t& length);
bool setSystemMetrics(CompanionEnvelope& message, const CompanionSystemMetrics& metrics);
bool readSystemMetrics(const CompanionEnvelope& message, CompanionSystemMetrics& metrics);
bool setAiUsage(CompanionEnvelope& message, const CompanionAiUsage& usage);
bool readAiUsage(const CompanionEnvelope& message, CompanionAiUsage& usage);
bool setSystemDetailsRequest(CompanionEnvelope& message, SystemDetailsGroup group);
bool readSystemDetailsRequest(const CompanionEnvelope& message, SystemDetailsGroup& group);
bool setSystemDetails(CompanionEnvelope& message, const CompanionSystemDetails& details);
bool readSystemDetails(const CompanionEnvelope& message, CompanionSystemDetails& details);

// INVENTORY_LIST request: start index (2). OK response: total (2), next index
// (2), count (1), then per entry: id (16), valid flag (1), revision (4), name
// length (1, 0 exactly when invalid) and UTF-8 name.
bool isInventoryOperation(CompanionOperation operation) noexcept;
std::size_t inventoryListEntrySize(std::size_t nameBytes) noexcept;
bool setInventoryListRequest(CompanionEnvelope& message, std::uint16_t start);
bool readInventoryListRequest(const CompanionEnvelope& message, std::uint16_t& start);
bool setInventoryListResponse(CompanionEnvelope& message, std::uint16_t total, std::uint16_t next,
                              const CompanionInventoryListEntry* entries, std::size_t count);
// INVENTORY_GET request: id (16), offset (2). OK response: revision (4), total
// (2), offset (2), 1..240 data bytes. `data` points into `message`.
bool setInventoryGetRequest(CompanionEnvelope& message, const CompanionInventoryId& id,
                            std::uint16_t offset);
bool readInventoryGetRequest(const CompanionEnvelope& message, CompanionInventoryId& id,
                             std::uint16_t& offset);
bool setInventoryGetResponse(CompanionEnvelope& message, const CompanionInventoryChunk& chunk);
bool readInventoryGetResponse(const CompanionEnvelope& message, CompanionInventoryChunk& chunk);
// INVENTORY_PUT request: id (16), expected revision (4), total (2), offset (2),
// 1..224 data bytes. OK response: bytes received (2), committed revision (4,
// 0 until the last chunk is committed). CONFLICT response: current revision (4).
bool setInventoryPutRequest(CompanionEnvelope& message, const CompanionInventoryChunk& chunk);
bool readInventoryPutRequest(const CompanionEnvelope& message, CompanionInventoryChunk& chunk);
bool setInventoryPutResponse(CompanionEnvelope& message, std::uint16_t received,
                             std::uint32_t committedRevision);
bool setInventoryConflict(CompanionEnvelope& message, std::uint32_t currentRevision);
// INVENTORY_DELETE request: id (16), the revision the editor saw (4; 0 for a
// stored file that is not a valid record). OK response: empty. CONFLICT
// response: current revision (4).
bool setInventoryDeleteRequest(CompanionEnvelope& message, const CompanionInventoryId& id,
                               std::uint32_t expectedRevision);
bool readInventoryDeleteRequest(const CompanionEnvelope& message, CompanionInventoryId& id,
                                std::uint32_t& expectedRevision);

} // namespace cardputer_hub::connectivity
