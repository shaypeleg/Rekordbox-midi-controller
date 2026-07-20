# Tasks

## In Progress

- [ ] Flash and MIDI-Learn test PAD FX: arm FX1-3, CFX Next/Back,
  MAP sliders for LevelDepth + CFX Parameter; then remove MAP helpers
- [ ] End-to-end test live playhead + zoom scroll: rkbx_link + companion +
  CYD TRACK expanded (jog / hotcue / pause); confirm yellow needle + scroll
- [ ] Map 7.2.16 deck hops 1–4 via `probe-hops` (only hop 0x8 verified)
- [ ] End-to-end: `./run.sh` + RB 7.2.16 + LIVE VIEW needle while scratching

## Completed

- [x] 2026-07-20 — Verified Mac 7.2.16 sample/BPM chain `B31CC40 8 550 120/188`
  and shipped `companion_app/data/offsets-macos-7.2.16` into rkbx_link merge.
- [x] 2026-07-20 — Added `companion_app/scripts/rb_memory_scan.py` to help
  reverse-engineer rkbx_link macOS offsets for newer Rekordbox versions.
- [x] 2026-07-19 — Live playhead via rkbx_link OSC: companion ingests
  `/N/time` on UDP 4460 and pushes `{"type":"playhead"}` at ~25 Hz;
  CYD TRACK draws a white needle on the ANLZ waveform without full redraws.
- [x] 2026-07-19 — `./run.sh` auto-vendors + launches rkbx_link
  (`ensure_rkbx_link.sh`, gitignored `.vendor/`, `--no-rkbx` escape hatch).
- [x] 2026-07-20 — `./run.sh -resign` wraps upstream resign_rekordbox.sh
  (macOS get-task-allow one-time setup).
- [x] 2026-07-20 — Menu split: LIVE VIEW (zoom), TRACK (comments),
  SCREENS (was VIEWS / RB VIEW → RB SCREENS).
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

- 2026-07-20 — Bridge snap-back caused playhead sawtooth; reconnect blocked
  Back button. Plateau bridge + one-step reconnect FSM.
- 2026-07-20 — Simplified LIVE VIEW data path: meta once, chunked 2048-col
  wave into RAM, playhead-only stream; local scroll/extrapolate. (Prior
  fat `wave_window` / embedded `wave_hi_b64` caused parse fails / OOM.)
- macOS rkbx_link community offsets only cover Rekordbox **7.2.8**.
  On 7.2.16: task port OK, then `Read memory failed: mach error: 1` —
  no OSC `/N/time`, CYD shows "No live playhead". See rkbx_link#55.
- Live playhead is not available over MIDI OUT or Pro DJ Link with a
  DDJ-REV5 in Performance Mode. Use rkbx_link (memory → OSC) instead.
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
