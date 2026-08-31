#include "core/input/keyboard_event_translator.h"

#include <algorithm>

namespace cardputer_hub::core {

void KeyboardEventTranslator::translate(const KeyboardSnapshot& snapshot, InputEvents& events) {
    events.clear();

    for (const auto& printable : snapshot.printableKeys) {
        if (!wasPrintableKeyPressed(printable.identity)) {
            events.push_back({InputEventType::PrintableCharacter, printable.character,
                              NamedKey::Tab, snapshot.modifiers});
        }
    }

    for (std::size_t index = 0; index < namedKeyCount; ++index) {
        if (snapshot.namedKeys[index] && !previous_.namedKeys[index]) {
            events.push_back(
                {InputEventType::NamedKey, '\0', static_cast<NamedKey>(index), snapshot.modifiers});
        }
    }

    previous_ = snapshot;
}

bool KeyboardEventTranslator::wasPrintableKeyPressed(std::uint16_t identity) const {
    return std::any_of(
        previous_.printableKeys.begin(), previous_.printableKeys.end(),
        [identity](const auto& printable) { return printable.identity == identity; });
}

} // namespace cardputer_hub::core
