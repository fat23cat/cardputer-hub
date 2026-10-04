# Third-Party Notices

## M5Stack NFC libraries

The NFC reader links four MIT-licensed libraries from M5Stack Technology CO LTD,
each pinned as a Git submodule under `components/` with its own `LICENSE` file:

| Component | Library | Pinned tag | Copyright |
| --- | --- | --- | --- |
| `components/m5unitnfc/upstream` | [M5Unit-NFC](https://github.com/m5stack/M5Unit-NFC) | 0.1.2 | 2025 M5Stack Technology CO LTD |
| `components/m5unitunified/upstream` | [M5UnitUnified](https://github.com/m5stack/M5UnitUnified) | 0.5.8 | 2024 M5Stack Technology CO LTD |
| `components/m5hal/upstream` | [M5HAL](https://github.com/m5stack/M5HAL) | 0.1.4 | 2024 M5Stack Technology CO LTD |
| `components/m5utility/upstream` | [M5Utility](https://github.com/m5stack/M5Utility) | 0.3.1 | 2024 M5Stack Technology CO LTD |

Their licenses permit use, copying and distribution provided the copyright and
permission notices are retained; the submodule `LICENSE` files carry them.
Cardputer Hub does not modify these libraries.

## System font (M5GFX Font0)

The system text font is built at runtime from Font0 of the pinned M5GFX managed
component, the 6×8 GLCD font by Adafruit Industries (BSD license, notice in
`managed_components/m5stack__m5gfx/src/lgfx/Fonts/glcdfont.h`). Cardputer Hub
replaces its unused upper half with Cyrillic and typographic glyphs drawn for
this project in `src/hardware/cardputer/assets/system_font_extension.h`; no
Font0 data is copied into this repository.

## Codex Microputer ADV

Cardputer Hub's synthesized key-click and directional value-step generator in
`scripts/generate_audio_clips.py`, its generated PCM asset in
`src/services/audio/assets/interface_clips.h`, and the Cardputer speaker setup
in `src/hardware/cardputer/cardputer_audio_adapter.cpp` are adapted from [Codex
Microputer ADV](https://github.com/fat23cat/codex-microputer-adv).

The adapted implementation was modified to use Cardputer Hub's Service and
hardware-adapter boundaries and to precompute bounded PCM clips during
development. The upstream work is licensed under the Apache License 2.0. A
copy is provided in [`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt).
