# Tasks

## In Progress

- [ ] Flash and MIDI-Learn test PAD FX: arm FX1-3, CFX Next/Back,
  MAP sliders for LevelDepth + CFX Parameter; then remove MAP helpers

## Completed

- [x] 2026-07-19 — Auto Cue: Deck Controls toggle that auto-enables
  headphone Cue for whichever deck the crossfader has just silenced.
  Added MIDI receive support (BLE onWrite + packet parser) so the device
  can read the crossfader's live value back from Rekordbox (Notes 28/29,
  CC 46). Requires a manual mapping-file row - see README.
- [x] 2026-07-15 — PAD FX setup uses FX1/2/3 arming (notes 30-35, same as
  Effects) + CFX Select Next/Back; pad gates like Effects paddle
- [x] 2026-07-15 — FX Pad Combo-style screen (setup + gated X/Y pad) on
  branch `feature/fx-pad-combo-xy`
- [x] 2026-07-15 — TEMP MAP screen with isolated X/Y sliders; menu layout
  5 performance / 3 views

## Discovered During Work

- Rekordbox cannot direct-select Beat FX or CFX type via MIDI Learn — only
  CFX Select Next/Back and FX slot On toggles.
- Sound Color / CFX Parameter must reset to MIDI value **64** on finger-up.
- FX Pad gate must reuse Effects notes 30-35 (`901E`-`9023`) so one mapping
  covers both the Effects paddle and the pad.
- MIDI Learn cannot isolate X vs Y from the pad finger — temporary
  single-axis sliders are required for reliable Learn.
- Rekordbox MIDI OUT is empty for all Knob/Slider rows (including
  CrossFader) — cannot use Rekordbox for fader feedback. Solved via
  companion `crossfader_relay.py` listening to DDJ-REV5 `B6 1F` directly.
