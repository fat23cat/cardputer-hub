#include <unity.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "connectivity/companion/companion_chunk_ring.h"
#include "connectivity/companion/companion_framer.h"
#include "connectivity/companion/companion_protocol.h"

namespace {

using cardputer_hub::connectivity::CompanionChunk;
using cardputer_hub::connectivity::CompanionChunkRing;
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

void test_encode_chunk_matches_bulk_encode() {
    CompanionFramer framer;
    const auto original = messageOf(40);
    std::array<CompanionChunk, companionMaxChunks> bulk{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(framer.encode(original, 10, bulk.data(), count, companionMaxChunks));
    TEST_ASSERT_EQUAL_UINT8(count, CompanionFramer::encodedChunkCount(original.size, 10));

    CompanionChunk one{};
    TEST_ASSERT_FALSE(framer.encodeChunk(original, 10, bulk[0].bytes[0], count, one));
    for (std::uint8_t index = 0; index < count; ++index) {
        TEST_ASSERT_TRUE(framer.encodeChunk(original, 10, bulk[0].bytes[0], index, one));
        TEST_ASSERT_EQUAL_UINT16(bulk[index].size, one.size);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(bulk[index].bytes.data(), one.bytes.data(), one.size);
    }
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

void test_maximum_message_can_arrive_over_three_seconds() {
    CompanionFramer framer;
    const auto original = messageOf(static_cast<std::uint16_t>(companionMaxMessageSize));
    std::array<CompanionChunk, companionMaxChunks> chunks{};
    std::uint8_t count = 0;
    TEST_ASSERT_TRUE(framer.encode(original, companionDefaultChunkPayload, chunks.data(), count,
                                   companionMaxChunks));
    TEST_ASSERT_EQUAL_UINT8(companionMaxChunks, count);
    for (std::uint8_t index = 0; index < count; ++index) {
        TEST_ASSERT_TRUE(framer.ingest(chunks[index].bytes.data(), chunks[index].size));
        if (index + 1 < count)
            framer.update(std::chrono::milliseconds(200));
    }
    const auto assembled = framer.take();
    TEST_ASSERT_TRUE(assembled.has_value());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.bytes.data(), assembled->bytes.data(), original.size);
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

std::array<std::uint8_t, 259> bytesOf(std::size_t size, std::uint8_t fill) {
    std::array<std::uint8_t, 259> bytes{};
    for (std::size_t index = 0; index < size; ++index)
        bytes[index] = static_cast<std::uint8_t>(fill + index);
    return bytes;
}

void test_chunk_ring_keeps_order_and_sizes_across_wrap() {
    // 64 bytes hold three 20-byte chunks (22 bytes each with the length).
    CompanionChunkRing<64> ring;
    CompanionChunk out{};
    TEST_ASSERT_FALSE(ring.pop(out));
    for (int round = 0; round < 10; ++round) {
        const auto a = bytesOf(20, static_cast<std::uint8_t>(round));
        const auto b = bytesOf(7, static_cast<std::uint8_t>(round + 100));
        TEST_ASSERT_TRUE(ring.push(a.data(), 20));
        TEST_ASSERT_TRUE(ring.push(b.data(), 7));
        TEST_ASSERT_EQUAL_UINT(2, ring.size());
        TEST_ASSERT_TRUE(ring.pop(out));
        TEST_ASSERT_EQUAL_UINT16(20, out.size);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(a.data(), out.bytes.data(), 20);
        TEST_ASSERT_TRUE(ring.pop(out));
        TEST_ASSERT_EQUAL_UINT16(7, out.size);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(b.data(), out.bytes.data(), 7);
        TEST_ASSERT_EQUAL_UINT(0, ring.size());
    }
}

void test_chunk_ring_rejects_what_does_not_fit_and_clears() {
    CompanionChunkRing<64> ring;
    const auto chunk = bytesOf(20, 1);
    TEST_ASSERT_TRUE(ring.push(chunk.data(), 20));
    TEST_ASSERT_TRUE(ring.push(chunk.data(), 20));
    TEST_ASSERT_TRUE(ring.push(chunk.data(), 18)); // 22 + 22 + 20 = 64 exactly
    TEST_ASSERT_FALSE(ring.push(chunk.data(), 1));
    TEST_ASSERT_EQUAL_UINT(3, ring.size());
    const auto large = bytesOf(259, 9);
    CompanionChunkRing<512> big;
    TEST_ASSERT_TRUE(big.push(large.data(), 259));
    TEST_ASSERT_FALSE(big.push(large.data(), 0));
    CompanionChunk out{};
    TEST_ASSERT_TRUE(big.pop(out));
    TEST_ASSERT_EQUAL_UINT16(259, out.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(large.data(), out.bytes.data(), 259);
    TEST_ASSERT_FALSE(big.push(large.data(), 260));
    ring.clear();
    TEST_ASSERT_EQUAL_UINT(0, ring.size());
    TEST_ASSERT_FALSE(ring.pop(out));
    TEST_ASSERT_TRUE(ring.push(chunk.data(), 20));
}

// The production ring holds two full AI_USAGE messages from a Companion that
// uses 17-byte chunks, and several MTU-sized chunks from a current Companion.
void test_production_ring_capacity() {
    CompanionChunkRing<cardputer_hub::connectivity::companionIncomingRingBytes> ring;
    const auto small = bytesOf(20, 3);
    for (int index = 0; index < 32; ++index)
        TEST_ASSERT_TRUE(ring.push(small.data(), 20));
    ring.clear();
    const auto large = bytesOf(259, 3);
    for (int index = 0; index < 5; ++index)
        TEST_ASSERT_TRUE(ring.push(large.data(), 259));
    static_assert(sizeof(ring) < 2048);
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_encode_chunk_matches_bulk_encode);
    RUN_TEST(test_single_chunk_round_trip);
    RUN_TEST(test_multiple_chunks_and_maximum_message);
    RUN_TEST(test_duplicate_chunk_and_invalid_index_are_rejected);
    RUN_TEST(test_inconsistent_chunk_count_and_oversized_message_are_rejected);
    RUN_TEST(test_reassembly_timeout_abandons_partial_frame);
    RUN_TEST(test_maximum_message_can_arrive_over_three_seconds);
    RUN_TEST(test_message_id_rollover_skips_zero);
    RUN_TEST(test_missing_chunk_does_not_complete);
    RUN_TEST(test_chunk_ring_keeps_order_and_sizes_across_wrap);
    RUN_TEST(test_chunk_ring_rejects_what_does_not_fit_and_clears);
    RUN_TEST(test_production_ring_capacity);
    return UNITY_END();
}
