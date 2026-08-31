#include "core/input/keyboard_event_translator.h"

#include <algorithm>

namespace cardputer_hub::core {

void KeyboardEventTranslator::translate(const KeyboardSnapshot& snapshot, InputEvents& events) {
    events.clear();

    for (const auto& key : snapshot.keys) {
        if (key.representation == KeyRepresentation::PrintableCharacter &&
            !wasPhysicalKeyPressed(key.identity)) {
            events.push_back({InputEventType::PrintableCharacter, key.character, NamedKey::Tab,
                              snapshot.modifiers});
        }
    }

    for (std::size_t index = 0; index < namedKeyCount; ++index) {
        const auto namedKey = static_cast<NamedKey>(index);
        for (const auto& key : snapshot.keys) {
            if (key.representation == KeyRepresentation::NamedKey && key.namedKey == namedKey &&
                !wasPhysicalKeyPressed(key.identity)) {
                events.push_back({InputEventType::NamedKey, '\0', namedKey, snapshot.modifiers});
            }
        }
    }

    previous_ = snapshot;
}

bool KeyboardEventTranslator::wasPhysicalKeyPressed(std::uint16_t identity) const {
    return std::any_of(previous_.keys.begin(), previous_.keys.end(),
                       [identity](const auto& key) { return key.identity == identity; });
}

} // namespace cardputer_hub::core
