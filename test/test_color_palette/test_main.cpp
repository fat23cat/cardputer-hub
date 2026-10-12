#include <unity.h>

#include "core/display/color_palette.h"
#include "core/display/palette.h"

using namespace cardputer_hub::core;

namespace {

void test_colors_get_stable_indices_in_order_of_first_use() {
    ColorPalette palette;
    const auto bone = palette.indexOf(palette::bone);
    const auto ink = palette.indexOf(palette::ink);
    TEST_ASSERT_EQUAL_UINT8(0, bone.index);
    TEST_ASSERT_TRUE(bone.added);
    TEST_ASSERT_EQUAL_UINT8(1, ink.index);
    TEST_ASSERT_TRUE(ink.added);
    const auto again = palette.indexOf(palette::bone);
    TEST_ASSERT_EQUAL_UINT8(0, again.index);
    TEST_ASSERT_FALSE(again.added);
    TEST_ASSERT_EQUAL_size_t(2, palette.size());
    TEST_ASSERT_EQUAL_UINT8(palette::ink.red, palette.color(1).red);
}

void test_a_full_palette_answers_with_the_nearest_color() {
    ColorPalette palette;
    for (int index = 0; index < 256; ++index) {
        const auto value = static_cast<std::uint8_t>(index);
        TEST_ASSERT_TRUE(palette.indexOf({value, value, value}).added);
    }
    TEST_ASSERT_EQUAL_size_t(ColorPalette::capacity, palette.size());
    // A new colour cannot be added; the closest grey stands in for it.
    const auto nearest = palette.indexOf({200, 202, 199});
    TEST_ASSERT_FALSE(nearest.added);
    TEST_ASSERT_EQUAL_UINT8(200, nearest.index);
    TEST_ASSERT_EQUAL_size_t(ColorPalette::capacity, palette.size());
}

void test_every_interface_color_fits_exactly() {
    ColorPalette palette;
    for (const auto& color :
         {palette::bone, palette::ink, palette::blue, palette::vermilion, palette::leaf,
          palette::pale, palette::lightNeutral, palette::homeWave, palette::ordinal}) {
        const auto found = palette.indexOf(color);
        TEST_ASSERT_TRUE(found.added);
        TEST_ASSERT_EQUAL_UINT8(color.green, palette.color(found.index).green);
    }
}

} // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_colors_get_stable_indices_in_order_of_first_use);
    RUN_TEST(test_a_full_palette_answers_with_the_nearest_color);
    RUN_TEST(test_every_interface_color_fits_exactly);
    return UNITY_END();
}
