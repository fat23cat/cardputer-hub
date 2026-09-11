#include <unity.h>

#include "core/input/keyboard_event_translator.h"
#include "hardware/cardputer/cardputer_adv_keyboard_layout.h"

namespace {

using cardputer_hub::core::InputEvents;
using cardputer_hub::core::InputEventType;
using cardputer_hub::core::KeyboardEventTranslator;
using cardputer_hub::core::KeyboardSnapshot;
using cardputer_hub::core::KeyRepresentation;
using cardputer_hub::core::Modifiers;
using cardputer_hub::core::NamedKey;
using cardputer_hub::core::PhysicalKeyState;
using cardputer_hub::hardware::cardputerAdvKeyboardSnapshot;
using cardputer_hub::hardware::CardputerAdvPressedKeys;
using cardputer_hub::hardware::decodeCardputerAdvKeyEvent;

constexpr NamedKey allNamedKeys[] = {
    NamedKey::Tab, NamedKey::Enter,      NamedKey::Backspace, NamedKey::Delete, NamedKey::Escape,
    NamedKey::Up,  NamedKey::Down,       NamedKey::Left,      NamedKey::Right,  NamedKey::F1,
    NamedKey::F2,  NamedKey::F3,         NamedKey::F4,        NamedKey::F5,     NamedKey::F6,
    NamedKey::F7,  NamedKey::F8,         NamedKey::F9,        NamedKey::F10,    NamedKey::F11,
    NamedKey::F12, NamedKey::SystemMenu,
};

PhysicalKeyState printable(std::uint16_t identity, char character) {
    return {identity, KeyRepresentation::PrintableCharacter, character, NamedKey::Tab};
}

PhysicalKeyState named(std::uint16_t identity, NamedKey key) {
    return {identity, KeyRepresentation::NamedKey, '\0', key};
}

PhysicalKeyState inactive(std::uint16_t identity) {
    return {identity, KeyRepresentation::Inactive, '\0', NamedKey::Tab};
}

void setPressed(KeyboardSnapshot& snapshot, NamedKey key, std::uint16_t identity) {
    snapshot.keys.push_back(named(identity, key));
}

} // namespace

void setUp() {}

void tearDown() {}

void test_printable_characters_preserve_vendor_order() {
    KeyboardEventTranslator translator;
    KeyboardSnapshot snapshot;
    snapshot.keys = {printable(31, 'b'), printable(12, 'a'), printable(44, ' ')};
    InputEvents events;

    translator.translate(snapshot, events);

    TEST_ASSERT_EQUAL_UINT(3, events.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(InputEventType::PrintableCharacter),
                            static_cast<unsigned int>(events[0].type));
    TEST_ASSERT_EQUAL_CHAR('b', events[0].character);
    TEST_ASSERT_EQUAL_CHAR('a', events[1].character);
    TEST_ASSERT_EQUAL_CHAR(' ', events[2].character);
}

void test_all_named_keys_are_emitted_in_enum_order() {
    KeyboardEventTranslator translator;
    KeyboardSnapshot snapshot;
    constexpr std::size_t keyCount = sizeof(allNamedKeys) / sizeof(allNamedKeys[0]);
    for (std::size_t index = keyCount; index > 0; --index) {
        const auto key = allNamedKeys[index - 1];
        setPressed(snapshot, key, static_cast<std::uint16_t>(index));
    }
    InputEvents events;

    translator.translate(snapshot, events);

    TEST_ASSERT_EQUAL_UINT(keyCount, events.size());
    for (std::size_t index = 0; index < events.size(); ++index) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(InputEventType::NamedKey),
                                static_cast<unsigned int>(events[index].type));
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(allNamedKeys[index]),
                                static_cast<unsigned int>(events[index].namedKey));
    }
}

void test_modifiers_are_attached_to_each_new_event() {
    KeyboardEventTranslator translator;
    KeyboardSnapshot snapshot;
    snapshot.modifiers = Modifiers{true, true, true, true, true};
    snapshot.keys = {printable(4, 'A')};
    setPressed(snapshot, NamedKey::Enter, 40);
    InputEvents events;

    translator.translate(snapshot, events);

    TEST_ASSERT_EQUAL_UINT(2, events.size());
    for (const auto& event : events) {
        TEST_ASSERT_TRUE(event.modifiers.shift);
        TEST_ASSERT_TRUE(event.modifiers.ctrl);
        TEST_ASSERT_TRUE(event.modifiers.alt);
        TEST_ASSERT_TRUE(event.modifiers.option);
        TEST_ASSERT_TRUE(event.modifiers.fn);
    }
}

void test_held_keys_are_not_reemitted_when_another_key_or_modifier_changes() {
    KeyboardEventTranslator translator;
    InputEvents events;
    KeyboardSnapshot first;
    first.keys = {printable(4, 'a')};
    translator.translate(first, events);
    TEST_ASSERT_EQUAL_UINT(1, events.size());

    KeyboardSnapshot second;
    second.modifiers.shift = true;
    second.keys = {printable(4, 'A'), printable(5, 'B')};
    setPressed(second, NamedKey::Left, 50);
    translator.translate(second, events);

    TEST_ASSERT_EQUAL_UINT(2, events.size());
    TEST_ASSERT_EQUAL_CHAR('B', events[0].character);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NamedKey::Left),
                            static_cast<unsigned int>(events[1].namedKey));
}

void test_release_followed_by_press_emits_a_new_event() {
    KeyboardEventTranslator translator;
    InputEvents events;
    KeyboardSnapshot pressed;
    pressed.keys = {printable(4, 'a')};
    setPressed(pressed, NamedKey::Enter, 40);
    translator.translate(pressed, events);
    TEST_ASSERT_EQUAL_UINT(2, events.size());

    KeyboardSnapshot released;
    translator.translate(released, events);
    TEST_ASSERT_EQUAL_UINT(0, events.size());

    translator.translate(pressed, events);
    TEST_ASSERT_EQUAL_UINT(2, events.size());
}

void test_held_key_is_not_reemitted_when_fn_changes_its_semantic_key() {
    KeyboardEventTranslator translator;
    InputEvents events;
    KeyboardSnapshot printableSnapshot;
    printableSnapshot.keys = {printable(30, '1')};
    translator.translate(printableSnapshot, events);
    TEST_ASSERT_EQUAL_UINT(1, events.size());

    KeyboardSnapshot functionKey;
    functionKey.modifiers.fn = true;
    setPressed(functionKey, NamedKey::F1, 30);
    translator.translate(functionKey, events);

    TEST_ASSERT_EQUAL_UINT(0, events.size());

    translator.translate(printableSnapshot, events);

    TEST_ASSERT_EQUAL_UINT(0, events.size());
}

void test_held_key_is_not_reemitted_after_an_inactive_fn_mapping() {
    KeyboardEventTranslator translator;
    InputEvents events;
    KeyboardSnapshot printableKey;
    printableKey.keys = {printable(4, 'a')};
    translator.translate(printableKey, events);
    TEST_ASSERT_EQUAL_UINT(1, events.size());

    KeyboardSnapshot inactiveFunctionKey;
    inactiveFunctionKey.modifiers.fn = true;
    inactiveFunctionKey.keys = {inactive(4)};
    translator.translate(inactiveFunctionKey, events);
    TEST_ASSERT_EQUAL_UINT(0, events.size());

    translator.translate(printableKey, events);

    TEST_ASSERT_EQUAL_UINT(0, events.size());
}

void test_translation_clears_the_callers_previous_events() {
    KeyboardEventTranslator translator;
    InputEvents events;
    events.push_back({InputEventType::PrintableCharacter, 'x', NamedKey::Tab, {}});

    translator.translate({}, events);

    TEST_ASSERT_EQUAL_UINT(0, events.size());
}

void test_tca8418_events_decode_to_cardputer_adv_coordinates_and_edges() {
    const auto firstPress = decodeCardputerAdvKeyEvent(0x81);
    TEST_ASSERT_TRUE(firstPress.has_value());
    TEST_ASSERT_EQUAL_UINT8(0, firstPress->row);
    TEST_ASSERT_EQUAL_UINT8(0, firstPress->column);
    TEST_ASSERT_TRUE(firstPress->pressed);

    const auto lastRelease = decodeCardputerAdvKeyEvent(68);
    TEST_ASSERT_TRUE(lastRelease.has_value());
    TEST_ASSERT_EQUAL_UINT8(3, lastRelease->row);
    TEST_ASSERT_EQUAL_UINT8(13, lastRelease->column);
    TEST_ASSERT_FALSE(lastRelease->pressed);

    TEST_ASSERT_FALSE(decodeCardputerAdvKeyEvent(0).has_value());
    TEST_ASSERT_FALSE(decodeCardputerAdvKeyEvent(9).has_value());
}

void test_cardputer_adv_layout_maps_normal_shift_and_named_keys() {
    CardputerAdvPressedKeys pressed{};
    pressed[1] = true;
    pressed[14] = true;
    pressed[29] = true;
    pressed[45] = true;

    const auto snapshot = cardputerAdvKeyboardSnapshot(pressed);

    TEST_ASSERT_TRUE(snapshot.modifiers.shift);
    TEST_ASSERT_EQUAL_UINT(3, snapshot.keys.size());
    TEST_ASSERT_EQUAL_CHAR('!', snapshot.keys[0].character);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NamedKey::Tab),
                            static_cast<unsigned int>(snapshot.keys[1].namedKey));
    TEST_ASSERT_EQUAL_CHAR('Z', snapshot.keys[2].character);
}

void test_cardputer_adv_fn_layer_maps_functions_arrows_and_inactive_keys() {
    CardputerAdvPressedKeys pressed{};
    pressed[1] = true;
    pressed[15] = true;
    pressed[28] = true;
    pressed[39] = true;
    pressed[52] = true;

    const auto snapshot = cardputerAdvKeyboardSnapshot(pressed);

    TEST_ASSERT_TRUE(snapshot.modifiers.fn);
    TEST_ASSERT_EQUAL_UINT(4, snapshot.keys.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NamedKey::F1),
                            static_cast<unsigned int>(snapshot.keys[0].namedKey));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(KeyRepresentation::Inactive),
                            static_cast<unsigned int>(snapshot.keys[1].representation));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NamedKey::Up),
                            static_cast<unsigned int>(snapshot.keys[2].namedKey));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned int>(NamedKey::Left),
                            static_cast<unsigned int>(snapshot.keys[3].namedKey));
}

void test_cardputer_adv_fn_tab_emits_one_settings_chord() {
    CardputerAdvPressedKeys pressed{};
    pressed[28] = true; // Fn
    pressed[14] = true; // Tab
    const auto snapshot = cardputerAdvKeyboardSnapshot(pressed);
    TEST_ASSERT_EQUAL_UINT(1, snapshot.keys.size());
    TEST_ASSERT_TRUE(snapshot.modifiers.fn);
    TEST_ASSERT_TRUE(snapshot.keys.front().representation == KeyRepresentation::NamedKey);
    TEST_ASSERT_TRUE(snapshot.keys.front().namedKey == NamedKey::Tab);
}

void test_cardputer_adv_layout_reports_all_modifiers() {
    CardputerAdvPressedKeys pressed{};
    pressed[28] = true;
    pressed[29] = true;
    pressed[42] = true;
    pressed[43] = true;
    pressed[44] = true;

    const auto snapshot = cardputerAdvKeyboardSnapshot(pressed);

    TEST_ASSERT_TRUE(snapshot.modifiers.fn);
    TEST_ASSERT_TRUE(snapshot.modifiers.shift);
    TEST_ASSERT_TRUE(snapshot.modifiers.ctrl);
    TEST_ASSERT_TRUE(snapshot.modifiers.option);
    TEST_ASSERT_TRUE(snapshot.modifiers.alt);
    TEST_ASSERT_TRUE(snapshot.keys.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_cardputer_adv_fn_tab_emits_one_settings_chord);
    RUN_TEST(test_printable_characters_preserve_vendor_order);
    RUN_TEST(test_all_named_keys_are_emitted_in_enum_order);
    RUN_TEST(test_modifiers_are_attached_to_each_new_event);
    RUN_TEST(test_held_keys_are_not_reemitted_when_another_key_or_modifier_changes);
    RUN_TEST(test_release_followed_by_press_emits_a_new_event);
    RUN_TEST(test_held_key_is_not_reemitted_when_fn_changes_its_semantic_key);
    RUN_TEST(test_held_key_is_not_reemitted_after_an_inactive_fn_mapping);
    RUN_TEST(test_translation_clears_the_callers_previous_events);
    RUN_TEST(test_tca8418_events_decode_to_cardputer_adv_coordinates_and_edges);
    RUN_TEST(test_cardputer_adv_layout_maps_normal_shift_and_named_keys);
    RUN_TEST(test_cardputer_adv_fn_layer_maps_functions_arrows_and_inactive_keys);
    RUN_TEST(test_cardputer_adv_layout_reports_all_modifiers);
    return UNITY_END();
}
