#include <unity.h>

#include <cstdint>

#include "hardware/cardputer/cardputer_puzzle_ws2812_adapter.h"

using cardputer_hub::core::LedHardwareFrame;
using cardputer_hub::core::ledMatrixPixelCount;
using cardputer_hub::core::ledMatrixWidth;
using cardputer_hub::hardware::IPuzzleLedBackend;
using cardputer_hub::hardware::puzzlePhysicalIndex;
using cardputer_hub::hardware::PuzzleRotation;
using cardputer_hub::hardware::puzzleWireIndex;
using cardputer_hub::hardware::PuzzleWs2812Adapter;

namespace {
void test_physical_mapping_is_bijective_and_in_range() {
    static constexpr PuzzleRotation rotations[] = {PuzzleRotation::Deg0, PuzzleRotation::Deg90,
                                                   PuzzleRotation::Deg180, PuzzleRotation::Deg270};
    for (auto rotation : rotations) {
        bool seen[ledMatrixPixelCount] = {};
        for (std::uint8_t y = 0; y < ledMatrixWidth; ++y) {
            for (std::uint8_t x = 0; x < ledMatrixWidth; ++x) {
                const auto physical = puzzleWireIndex(x, y, rotation);
                TEST_ASSERT_TRUE(physical < ledMatrixPixelCount);
                TEST_ASSERT_FALSE(seen[physical]);
                seen[physical] = true;
            }
        }
        for (bool used : seen)
            TEST_ASSERT_TRUE(used);
    }
}

void test_upright_mapping_matches_unit_puzzle_column_major_order() {
    TEST_ASSERT_EQUAL_UINT8(7, puzzleWireIndex(0, 0));
    TEST_ASSERT_EQUAL_UINT8(56, puzzleWireIndex(7, 7));
    TEST_ASSERT_EQUAL_UINT8(63, puzzleWireIndex(7, 0));
    TEST_ASSERT_EQUAL_UINT8(0, puzzleWireIndex(0, 7));
    TEST_ASSERT_EQUAL_UINT8(6, puzzleWireIndex(0, 1));
    TEST_ASSERT_EQUAL_UINT8(7, puzzlePhysicalIndex(0));
    TEST_ASSERT_EQUAL_UINT8(56, puzzlePhysicalIndex(63));
}

void test_rotation_moves_logical_origin() {
    TEST_ASSERT_EQUAL_UINT8(63, puzzleWireIndex(0, 0, PuzzleRotation::Deg90));
    TEST_ASSERT_EQUAL_UINT8(56, puzzleWireIndex(0, 0, PuzzleRotation::Deg180));
    TEST_ASSERT_EQUAL_UINT8(0, puzzleWireIndex(0, 0, PuzzleRotation::Deg270));
}

class FakePuzzleBackend final : public IPuzzleLedBackend {
  public:
    void quietLine() override { ++quiets; }
    bool open() override {
        ++opens;
        return openOk;
    }
    bool writeMappedFrame(const LedHardwareFrame&) override {
        ++writes;
        return writeOk;
    }
    void close() override { ++closes; }

    bool openOk = true;
    bool writeOk = true;
    int quiets = 0;
    int opens = 0;
    int writes = 0;
    int closes = 0;
};

void test_begin_failure_is_retryable() {
    FakePuzzleBackend backend;
    backend.openOk = false;
    PuzzleWs2812Adapter adapter(backend);
    TEST_ASSERT_FALSE(adapter.begin());
    TEST_ASSERT_EQUAL(1, backend.opens);
    TEST_ASSERT_TRUE(backend.quiets >= 2);
    backend.openOk = true;
    TEST_ASSERT_TRUE(adapter.begin());
    TEST_ASSERT_EQUAL(2, backend.opens);
}

void test_write_failure_returns_adapter_to_retryable_state() {
    FakePuzzleBackend backend;
    PuzzleWs2812Adapter adapter(backend);
    LedHardwareFrame frame{};
    adapter.writeFrame(frame);
    TEST_ASSERT_EQUAL(1, backend.opens);
    TEST_ASSERT_EQUAL(1, backend.writes);
    backend.writeOk = false;
    adapter.writeFrame(frame);
    TEST_ASSERT_EQUAL(1, backend.closes);
    TEST_ASSERT_EQUAL(2, backend.writes);
    backend.writeOk = true;
    adapter.writeFrame(frame);
    TEST_ASSERT_EQUAL(2, backend.opens);
    TEST_ASSERT_EQUAL(3, backend.writes);
}
} // namespace

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_physical_mapping_is_bijective_and_in_range);
    RUN_TEST(test_upright_mapping_matches_unit_puzzle_column_major_order);
    RUN_TEST(test_rotation_moves_logical_origin);
    RUN_TEST(test_begin_failure_is_retryable);
    RUN_TEST(test_write_failure_returns_adapter_to_retryable_state);
    return UNITY_END();
}
