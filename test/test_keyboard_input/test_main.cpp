#include <unity.h>

#include "core/input/keyboard_event_translator.h"

namespace {

using cardputer_hub::core::InputEvents;
using cardputer_hub::core::InputEventType;
using cardputer_hub::core::KeyboardEventTranslator;
using cardputer_hub::core::KeyboardSnapshot;
using cardputer_hub::core::Modifiers;
using cardputer_hub::core::NamedKey;
using cardputer_hub::core::PrintableKeyState;

constexpr NamedKey allNamedKeys[] = {
    NamedKey::Tab, NamedKey::Enter, NamedKey::Backspace, NamedKey::Delete, NamedKey::Escape,
    NamedKey::Up,  NamedKey::Down,  NamedKey::Left,      NamedKey::Right,  NamedKey::F1,
    NamedKey::F2,  NamedKey::F3,    NamedKey::F4,        NamedKey::F5,     NamedKey::F6,
    NamedKey::F7,  NamedKey::F8,    NamedKey::F9,        NamedKey::F10,    NamedKey::F11,
    NamedKey::F12,
};

void setPressed(KeyboardSnapshot& snapshot, NamedKey key) {
    snapshot.namedKeys[static_cast<std::size_t>(key)] = true;
}

} // namespace

void setUp() {}

void tearDown() {}

void test_printable_characters_preserve_vendor_order() {
    KeyboardEventTranslator translator;
    KeyboardSnapshot snapshot;
    snapshot.printableKeys = {{31, 'b'}, {12, 'a'}, {44, ' '}};
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
    for (const auto key : allNamedKeys) {
        setPressed(snapshot, key);
    }
    InputEvents events;

    translator.translate(snapshot, events);

    TEST_ASSERT_EQUAL_UINT(sizeof(allNamedKeys) / sizeof(allNamedKeys[0]), events.size());
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
    snapshot.printableKeys = {{4, 'A'}};
    setPressed(snapshot, NamedKey::Enter);
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
    first.printableKeys = {{4, 'a'}};
    translator.translate(first, events);
    TEST_ASSERT_EQUAL_UINT(1, events.size());

    KeyboardSnapshot second;
    second.modifiers.shift = true;
    second.printableKeys = {{4, 'A'}, {5, 'B'}};
    setPressed(second, NamedKey::Left);
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
    pressed.printableKeys = {{4, 'a'}};
    setPressed(pressed, NamedKey::Enter);
    translator.translate(pressed, events);
    TEST_ASSERT_EQUAL_UINT(2, events.size());

    KeyboardSnapshot released;
    translator.translate(released, events);
    TEST_ASSERT_EQUAL_UINT(0, events.size());

    translator.translate(pressed, events);
    TEST_ASSERT_EQUAL_UINT(2, events.size());
}

void test_translation_clears_the_callers_previous_events() {
    KeyboardEventTranslator translator;
    InputEvents events;
    events.push_back({InputEventType::PrintableCharacter, 'x', NamedKey::Tab, {}});

    translator.translate({}, events);

    TEST_ASSERT_EQUAL_UINT(0, events.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_printable_characters_preserve_vendor_order);
    RUN_TEST(test_all_named_keys_are_emitted_in_enum_order);
    RUN_TEST(test_modifiers_are_attached_to_each_new_event);
    RUN_TEST(test_held_keys_are_not_reemitted_when_another_key_or_modifier_changes);
    RUN_TEST(test_release_followed_by_press_emits_a_new_event);
    RUN_TEST(test_translation_clears_the_callers_previous_events);
    return UNITY_END();
}
