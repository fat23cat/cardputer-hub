#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "connectivity/companion/companion_framer.h"
#include "connectivity/companion/companion_protocol.h"

namespace {

using cardputer_hub::connectivity::CompanionChunk;
using cardputer_hub::connectivity::companionDefaultChunkPayload;
using cardputer_hub::connectivity::CompanionEncodedMessage;
using cardputer_hub::connectivity::CompanionFramer;
using cardputer_hub::connectivity::companionMaxChunks;
using cardputer_hub::connectivity::companionMaxMessageSize;
using cardputer_hub::connectivity::companionReassemblyTimeout;

CompanionEncodedMessage messageOf(std::uint16_t size, std::uint8_t fill = 0xA5) {
    CompanionEncodedMessage message{};
    message.size = size;
    for (std::uint16_t index = 0; index < size; ++index) {
        message.bytes[index] = static_cast<std::uint8_t>(fill + index);
    }
    return message;
}

void test_single_chunk_round_trip() {
    CompanionFramer framer;
    const auto original = messageOf(8);
    std::array<CompanionChunk, 1> chunks{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(
        framer.encode(original, companionDefaultChunkPayload, chunks.data(), count, 1));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_TRUE(framer.ingest(chunks[0].bytes.data(), chunks[0].size));
    const auto assembled = framer.take();
    TEST_ASSERT_TRUE(assembled.has_value());
    TEST_ASSERT_EQUAL_UINT16(original.size, assembled->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.bytes.data(), assembled->bytes.data(), original.size);
}

void test_multiple_chunks_and_maximum_message() {
    CompanionFramer framer;
    const auto original = messageOf(static_cast<std::uint16_t>(companionMaxMessageSize));
    std::array<CompanionChunk, companionMaxChunks> chunks{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(framer.encode(original, companionDefaultChunkPayload, chunks.data(), count,
                                   companionMaxChunks));
    TEST_ASSERT_TRUE(count > 1);
    TEST_ASSERT_TRUE(count <= companionMaxChunks);
    for (std::uint8_t index = 0; index < count; ++index) {
        TEST_ASSERT_TRUE(framer.ingest(chunks[index].bytes.data(), chunks[index].size));
    }
    const auto assembled = framer.take();
    TEST_ASSERT_TRUE(assembled.has_value());
    TEST_ASSERT_EQUAL_UINT16(original.size, assembled->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.bytes.data(), assembled->bytes.data(), original.size);
}

void test_duplicate_chunk_and_invalid_index_are_rejected() {
    CompanionFramer framer;
    const auto original = messageOf(40);
    std::array<CompanionChunk, companionMaxChunks> chunks{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(framer.encode(original, 10, chunks.data(), count, companionMaxChunks));
    TEST_ASSERT_TRUE(count > 1);
    TEST_ASSERT_TRUE(framer.ingest(chunks[0].bytes.data(), chunks[0].size));
    TEST_ASSERT_FALSE(framer.ingest(chunks[0].bytes.data(), chunks[0].size));
    TEST_ASSERT_FALSE(framer.take().has_value());

    CompanionFramer invalid;
    TEST_ASSERT_TRUE(invalid.encode(original, 10, chunks.data(), count, companionMaxChunks));
    chunks[0].bytes[1] = 15;
    TEST_ASSERT_FALSE(invalid.ingest(chunks[0].bytes.data(), chunks[0].size));
}

void test_inconsistent_chunk_count_and_oversized_message_are_rejected() {
    CompanionFramer framer;
    const auto original = messageOf(40);
    std::array<CompanionChunk, companionMaxChunks> chunks{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(framer.encode(original, 10, chunks.data(), count, companionMaxChunks));
    TEST_ASSERT_TRUE(framer.ingest(chunks[0].bytes.data(), chunks[0].size));
    chunks[1].bytes[2] = static_cast<std::uint8_t>(chunks[1].bytes[2] + 1);
    TEST_ASSERT_FALSE(framer.ingest(chunks[1].bytes.data(), chunks[1].size));

    CompanionEncodedMessage oversized{};
    oversized.size = companionMaxMessageSize;
    CompanionFramer encoder;
    TEST_ASSERT_FALSE(encoder.encode(oversized, 8, chunks.data(), count, companionMaxChunks));
}

void test_reassembly_timeout_abandons_partial_frame() {
    CompanionFramer framer;
    const auto original = messageOf(40);
    std::array<CompanionChunk, companionMaxChunks> chunks{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(framer.encode(original, 10, chunks.data(), count, companionMaxChunks));
    TEST_ASSERT_TRUE(framer.ingest(chunks[0].bytes.data(), chunks[0].size));
    framer.update(companionReassemblyTimeout);
    TEST_ASSERT_TRUE(framer.ingest(chunks[1].bytes.data(), chunks[1].size));
    TEST_ASSERT_FALSE(framer.take().has_value());
}

void test_message_id_rollover_skips_zero() {
    CompanionFramer framer;
    const auto original = messageOf(8);
    std::array<CompanionChunk, 1> chunks{};
    std::uint8_t count = 0;
    std::uint8_t previous = 0;
    for (int index = 0; index < 260; ++index) {
        TEST_ASSERT_TRUE(framer.encode(original, 20, chunks.data(), count, 1));
        TEST_ASSERT_NOT_EQUAL(0, chunks[0].bytes[0]);
        if (previous == 255) {
            TEST_ASSERT_EQUAL_UINT8(1, chunks[0].bytes[0]);
        }
        previous = chunks[0].bytes[0];
    }
}

void test_missing_chunk_does_not_complete() {
    CompanionFramer framer;
    const auto original = messageOf(40);
    std::array<CompanionChunk, companionMaxChunks> chunks{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(framer.encode(original, 10, chunks.data(), count, companionMaxChunks));
    TEST_ASSERT_TRUE(count >= 3);
    TEST_ASSERT_TRUE(framer.ingest(chunks[0].bytes.data(), chunks[0].size));
    TEST_ASSERT_TRUE(framer.ingest(chunks[2].bytes.data(), chunks[2].size));
    TEST_ASSERT_FALSE(framer.take().has_value());
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_single_chunk_round_trip);
    RUN_TEST(test_multiple_chunks_and_maximum_message);
    RUN_TEST(test_duplicate_chunk_and_invalid_index_are_rejected);
    RUN_TEST(test_inconsistent_chunk_count_and_oversized_message_are_rejected);
    RUN_TEST(test_reassembly_timeout_abandons_partial_frame);
    RUN_TEST(test_message_id_rollover_skips_zero);
    RUN_TEST(test_missing_chunk_does_not_complete);
    return UNITY_END();
}
