# Third-Party Notices

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
