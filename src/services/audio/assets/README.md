# Interface audio clips

`interface_clips.h` contains the bounded 16 kHz signed PCM buffers used by
`AudioService`. They are generated from the adapted Codex Microputer ADV cue
synthesis during development so firmware startup performs no waveform math and
the constant samples remain in flash. Each clip fades out before a final 8 ms
digital-silence tail, preventing an amplifier transient when playback ends.

Regenerate and verify the asset with:

```sh
python scripts/generate_audio_clips.py src/services/audio/assets/interface_clips.h
python scripts/generate_audio_clips.py --check src/services/audio/assets/interface_clips.h
```

The upstream attribution and Apache-2.0 notice are recorded in the repository's
`THIRD_PARTY_NOTICES.md` and `LICENSES/Apache-2.0.txt`.
