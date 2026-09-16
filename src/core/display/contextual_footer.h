#pragma once

#include "core/display/display_adapter.h"
#include "core/display/palette.h"
#include "core/display/text_layout.h"

namespace cardputer_hub::core {

inline void drawContextualFooter(IDisplayAdapter& display, const char* cancel,
                                 const char* confirm = "") {
    const TextStyle quiet{palette::ordinal, palette::bone, 1};
    if (cancel != nullptr && *cancel != '\0')
        display.drawText({6, 123}, cancel, quiet);
    if (confirm != nullptr && *confirm != '\0')
        display.drawText({rightAlignedTextX(confirm), 123}, confirm, quiet);
}

} // namespace cardputer_hub::core
