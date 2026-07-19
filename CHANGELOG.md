# Changelog

All notable changes to this project are documented in this file.

## 2026-07-19

### Fixed - Auto Cue both-channels-on after fast crossfader moves

Fast left↔right swipes could leave both headphone Cues on in Rekordbox
even though the CYD's local mirror looked correct. Cause: Cue is a Note On
*toggle*, and a fast swipe fired LEFT → NONE (off) → RIGHT (on) while the
relay flooded BLE with every crossfader CC — a dropped "off" toggle left
the previous Cue stuck on.

- Firmware: hysteresis + 50ms debounce before treating the middle as
  "release Cue"; enter opposite end immediately (skip the off/on pair);
  always send unwanted Cue off before wanted Cue on.
- Companion relay: send only zone changes (`0` / `64` / `127`) instead of
  every MSB update.

### Added - Auto Cue (crossfader-driven headphone monitor)

New **AUTO CUE** toggle on the Deck Controls screen. When enabled, the
device watches the crossfader position and automatically turns on
headphone Cue for whichever deck the crossfader has just silenced (full
left → Deck 2 Cue on, full right → Deck 1 Cue on), turning it back off
once the fader leaves that end.

This required adding MIDI **receive** support for the first time - every
other feature in this sketch only sends MIDI. The crossfader lives on the
DDJ-REV5, not on this device, so Rekordbox has to feed its live position
back to "RB-MIDI" over BLE MIDI:

- `midi_utils.h`: `lastReceivedCC[]` cache + `handleIncomingMIDIPacket()`,
  a minimal BLE-MIDI packet parser (no running-status support - not needed
  for a single feedback CC).
- `Rekordbox-Midi-Controller.ino`: `MIDIRxCallbacks` (`onWrite`) wired to
  the existing characteristic (already had `WRITE_NR`), `initMIDIRx()` in
  `setup()`, `updateAutoCue()` in `loop()` (runs regardless of the active
  screen, since this is a background safety behaviour).
- `common_definitions.h`: Auto Cue sends DDJ-REV5 Cue codes `9007` /
  `9107` (Note 7 on ch1/ch2); `CC_CROSSFADER_FEEDBACK` (46) for the
  incoming crossfader value - the first *incoming* constant in the file.
- `deck_controls_mode.h`: edge-triggered zone detection (`XF_ZONE_LEFT` /
  `XF_ZONE_RIGHT` / `XF_ZONE_NONE`) so Cue notes are sent once per
  crossing, not on every poll; a live `XFADE:` / Cue-state status line for
  verifying the mapping; toggle rows shrunk slightly (38px → 30px) to fit
  the new status line + toggle in the existing 240px screen height.

Rekordbox does not populate MIDI OUT for Knob/Slider functions (including
CrossFader), so feedback cannot come from Rekordbox. A companion-app
relay (`crossfader_relay.py`) instead listens to the DDJ-REV5's native
crossfader MIDI (`B6 1F` MSB, verified live) and forwards CC 46 to
RB-MIDI Bluetooth. See README → "Auto Cue: crossfader feedback setup".

## 2026-07-15

### Added - FX Pad (Rekordbox Combo FX style)

New **PAD FX** main-menu screen for gated Beat FX + Color FX on an X/Y
touch pad (inspired by DDJ-RZX Combo FX), adapted to Rekordbox MIDI Learn
limits (no direct FX/CFX type select — only Next/Back for CFX):

1. **Setup screen** — arm any combo of FX1/FX2/FX3 on Deck 1 and Deck 2
   (same Notes 30–35 as the Effects screen / `FX1-1On`…`FX2-3On`). Cycle
   Color FX with Prev CFX / Next CFX (Notes 95 / 94 → CFX Select Back/Next).
   Then tap **DECK1** or **DECK2** (pad controls that deck only).
2. **Pad screen** — full-bleed dark X/Y pad with crosshair and glowing cursor.
   - Finger down → toggle that deck's armed FX On (Effects-paddle behaviour) + drive CCs
   - **X axis** → LevelDepth CC for every armed FX slot on that deck
     (D1: CC 90/91/96, D2: CC 97/98/99)
   - **Y axis** → CFX Parameter (CC 92 or 93 for the active deck; center ≈ 64 = off)
   - Finger up → toggle FX Off, armed LevelDepth → 0, CFX Parameter → 64
3. **MAP screen** — hidden by default (`FXPAD_SHOW_MAP`); D1/D2 Level + CFX Learn helpers kept in code.

Menu order: FX, **PAD FX**, Decks, Stems, HotCue (row 1 performance);
Track, Scroll, Views (row 2 views).

Finger must stay on the pad for effects to apply; lifting the finger removes
both. Back on the pad returns to setup; back on setup returns to the menu.

### Changed - PAD FX setup: hide MAP, DECK1/DECK2 labels, more CFX spacing

- Hidden the MAP button (`FXPAD_SHOW_MAP 0`; set to 1 to bring Learn helpers back).
- Renamed START 1 / START 2 to **DECK1** / **DECK2**.
- Increased space between FX1/2/3 buttons and the COLOR FX row.

### Changed - PAD FX touch glow

djay-style pixelated neon blue touch blob (4px blocks, no circle outline),
plus a fading pixelated trail that follows finger movement.

### Fixed - PAD FX On notes dropped when Level CCs followed immediately

Touching the pad sent FX On (Notes 30–35) then LevelDepth CCs with no gap
after the last Note. NimBLE notify on one characteristic dropped that Note,
so a single armed FX never turned on, and with two armed only the first did.
Increased MIDI spacing to 40ms and added a trailing gap after FX On toggles
(and after Level bursts) before the next message.

## 2026-07-09

### Fixed - WiFi scan list empty until RESCAN

The first Setup → WiFi scan often showed no networks because ESP32 cannot
scan reliably while a boot-time `WiFi.begin()` reconnect is still running.
`scanNetworks()` now disconnects any in-progress connection, clears a stale
scan, and retries once so the list appears on the first open.

## 2026-07-07

### Changed - Improved spacing and larger comment font in track views

Compact (dual-deck) view:
- Increased gap between title/BPM row and waveform top from 2px to 5px.
- Increased gap between waveform bottom and comment from 2px to 5px.

Expanded (single-deck) view:
- Increased gap between waveform bottom and hot cue legend from 3px to 6px.
- Increased gap between hot cue legend and comment from 18px to 22px.
- Comment font increased from font 2 (16px) to font 4 (26px) for ~1.5x
  readability improvement.

### Changed - Taller waveforms in compact (dual deck) view

Increased the waveform height in the compact two-deck view from 36px to
42px per deck. The extra 12px total is absorbed by the unused space at
the bottom of the screen.

### Changed - Comment text scrolls when it overflows the screen

Comments that are too long to fit on one line now use the same ping-pong
marquee animation as track titles (pauses 1.5s at each end, scrolls at
2px/50ms). Applies to both compact (dual-deck) and expanded (single-deck)
views. Removes the old fixed character-count truncation — the full comment
is always accessible via the scroll.

Reverted the expanded-view comment font from 4 back to 2 (font 4 was too
large for the display).

## 2026-07-06

### Changed - Hot cue legend shows labels/time instead of letters

The hot cue legend below the waveform now displays useful information:
- **If a cue has a label** (e.g. "8 IN", "8 OUT", "DROP"): shows the label text
- **If no label**: shows the cue's formatted timestamp (e.g. "1:32")
- Visual style preserved: white text on colored background pill matching the
  waveform marker color, so the DJ can associate legend entries with cue points.
- Variable-width pills with progressive overflow handling: reduces gaps/padding
  first, then truncates longest labels if needed.
- Tap-to-expand: tapping a truncated pill reveals the full label text.
- Legend now appears in both compact (font 1) and expanded (font 2) views.
- All cues are always shown - never filtered or hidden.

### Changed - Track comment two-tone coloring (entry vs exit transitions)

Comments on the Track Info screen now split at `>>` into two colors:
- **Before `>>`** (entry transition): yellow/amber (`THEME_WARNING`)
- **After `>>`** (exit transition): cyan (`THEME_ACCENT`)

For example a comment like `96 > STEM M+B >> Echo 1 out` renders the
BPM/stem info in yellow and the echo-out instruction in cyan, so the DJ
can quickly distinguish how to enter vs. leave each track. Comments
without `>>` render entirely in yellow as before. Applies to both compact
and expanded deck views via the shared `tiDrawComment()` helper.

### Changed - Main menu reorder, renames, and centered rows

Reorganised the main menu grid for a clearer two-row layout:

- **Row 1**: FX, Decks, HotCue, Stems (controller functions)
- **Row 2**: Track, Scroll, Views (display/navigation functions)
- Renamed "SEARCH" to "SCROLL" (the screen scrolls on the waveform, not searches).
- Renamed "VIEW" to "VIEWS".
- Partial rows (e.g. 3 items in row 2) are now horizontally centred instead of
  left-aligned, using new `menuItemX()`/`menuItemY()` helpers shared by both
  `drawMenu()` and `handleMenuTouch()`.

### Changed - Track Info screen UX overhaul (compact/expanded views, non-blocking back)

Redesigned the Track Info screen for better usability on the small 2.8" CYD display:

- **Back button always responsive**: All blocking operations (mDNS init,
  service discovery, WebSocket connect) are deferred from `drawTrackInfoMode()`
  into the handle loop. The draw function now returns instantly (just draws
  the back chevron + status text), so the back button is responsive from the
  very first loop iteration. Previously the screen blocked for 2+ seconds on
  mDNS queries during which touch input was completely lost.
- **Status text no longer overlaps track data**: The "Connecting..."/"Searching..."
  messages only display when no waveform data has been received yet, preventing
  the status from rendering on top of deck B's content area.
- **Removed "TRACK INFO" header bar**: Freed ~48px of vertical space by removing
  the full header. Only the back chevron remains in the top-left corner.
- **Two-state view (compact/expanded)**:
  - **Compact** (default): Both decks visible with deck label, title, BPM/Key,
    waveform with hot cue markers, and comment (font 2, amber) on one screen.
  - **Expanded**: Tap either deck to expand full-screen. Shows title (scrolling),
    BPM/Key/Duration in large font (font 4), tall 72px waveform, hot cue legend
    with wider colored boxes (font 2 letters), and comment (font 2, amber).
    Tap anywhere to return to compact view.
- **Swap A/B button**: Arrow icon (↑↓) in the top-right corner lets the user
  manually swap deck A/B assignment when automatic detection gets it wrong.
  Turns amber when swapped so the DJ knows the override is active.
- **Content moved below back button**: `TI_CONTENT_Y` raised from 30 to 48 so
  track data never overlaps the back chevron.
- **Comment text highlighted**: Comments use `THEME_WARNING` (amber) color and
  font 2 in both views since they contain critical DJ transition instructions.
- **BPM/Key/Duration enlarged**: In expanded view these use font 4 (26px) for
  at-a-glance readability. In compact view, BPM+Key use font 2 (was font 1).
- **Hot cue legend readable**: Boxes are 20x14px with font 2 centered letters
  (was 16x12 font 1), no longer cut off.
- **Scrolling title marquee**: Long track titles that overflow the available
  width now animate with a ping-pong scroll (pauses 1.5s at each end, scrolls
  at 2px/50ms). Works in both compact and expanded views. Scroll resets when
  a new track is loaded.
- Refactored waveform and cue marker rendering into shared helper functions
  (`tiDrawWaveform`, `tiDrawCueMarkers`) for reuse in both view states.

### Fixed - Instant double detection during runtime in companion server

Loading the same track on both decks (instant double) now works correctly
during a live session, not just at startup. Previously the server treated the
new FD as a "reload" of the existing deck because the path already matched.

- Track per-path FD count between polls (`prev_path_fd_count`). When a new FD
  appears for a path already on one deck AND the FD count for that path
  increased, it's recognized as an instant double on the other deck.
- Priority 3 (fill empty deck slots) now allows assigning a path already on the
  other deck if its FD count is >= 2, catching instant doubles where the old
  track's closure comes in a later poll than the new FD appearance.

### Fixed - Track detection sync issues in companion server

- **DeckTracker state machine**: Rewrote track detection to use temporal FD
  tracking instead of naive "first 2 files from lsof." Rekordbox keeps
  previously loaded files cached (open file descriptors), so lsof often shows
  4+ audio files. The new `DeckTracker` class watches for NEW file descriptors
  appearing between polls to identify actual deck load events.
- **Reload detection**: Reloading the same track (same path, new FD) is now
  correctly detected and pushes an update to the CYD.
- **Quick replacement handling**: Loading track X then immediately replacing
  with track Y on the same deck uses a timing heuristic (loads within 5s go
  to the same deck slot; spaced loads alternate).
- **Instant double at startup**: If the server starts while an instant double
  is active (same path with 2+ FDs), both decks correctly show that track.
- **Faster polling**: Reduced poll interval from 1.5s to 1.0s.
- **mDNS resilience**: Server no longer crashes on restart if previous mDNS
  registration is still cached on the network.
- **Known limitation**: On server startup with cached files, the initial deck
  guess (highest 2 FDs) may be incorrect. Self-corrects on the first track
  load after startup.

### Added - Track Info screen (real-time now-playing display from Rekordbox)

New "TRACK" screen accessible from the main menu (cyan icon) that displays
real-time track metadata from Rekordbox on the CYD, including:

- **Per-deck display**: title, artist, BPM, key, comment for both loaded decks
- **Color waveform**: 320-pixel-wide RGB waveform rendered from Rekordbox's
  PWV5 analysis data, drawn with per-column vertical lines
- **Hot cue overlay**: colored boxes with pad letters (A-H) positioned at the
  correct time offset on the waveform

Requires the companion Python server running on the same network as the CYD.
The ESP32 discovers the server automatically via mDNS (zero configuration).

Architecture:
- `companion_app/nowplaying_server.py`: Python WebSocket server that monitors
  Rekordbox via `lsof` (detects open audio files), queries `pyrekordbox` for
  metadata/waveform/cues, and pushes JSON updates to connected clients.
- `track_info_mode.h`: ESP32 WebSocket client with mDNS discovery, JSON
  parsing (ArduinoJson), and TFT rendering.
- Communication: WebSocket on port 9100, auto-discovered via
  `_rekordbox-cyd._tcp.local` mDNS service.

New dependencies:
- ESP32: `ArduinoWebsockets` (Gil Maimon), `ArduinoJson` (Benoit Blanchon)
- Python: `pyrekordbox`, `websockets`, `zeroconf`

**Important**: Partition scheme must be changed to "Huge APP (3MB No OTA /
1MB SPIFFS)" due to flash size constraints. The default 1.2MB partition is
at 94% capacity without this feature.

### Changed - Main menu grid now wraps to 2 rows

The menu icon grid now supports wrapping to multiple rows (4 icons per row)
to accommodate the 7th app icon (TRACK). Previously it was a single row of 6.

## 2026-07-05

### Changed - Main menu icon order

Reordered the main menu icon grid to: FX, Decks, Hot Cue, Search, Stems,
View. Hot Cue moved from position 6 to position 3, pushing Search, Stems,
and View one slot to the right.

### Changed - Hot Cue buttons are now gated (press-and-hold like REV5 pads)

Hot cue buttons now send Note On (`0x90`) when pressed and Note Off (`0x80`)
when released, matching the gated behavior of the DDJ-REV5's hot cue pads.
Previously they sent only Note On (via `sendToggleNote`), which caused
Rekordbox to start playback but left the play state out of sync — the play
button on the REV5 wasn't solid and needed two presses to regain control.

- Button stays visually inverted (hollow with colored border) while held.
- Releasing the finger restores the filled button and sends Note Off.
- Navigating back while a button is held sends Note Off before exiting.

### Added - Second mode-entry signal on Hot Cue screen for Rekordbox view switch

Entering the Hot Cue screen now sends two MIDI notes back-to-back (20ms gap):
1. `NOTE_HOTCUE_MODE_ENTER` (86) — for switching to Hot Cue pad mode.
2. `NOTE_HOTCUE_VIEW_SWITCH` (87) — for switching screen view in Rekordbox.

Map each to a separate function in Rekordbox MIDI Learn.

### Added - Hot Cue screen (8 pads per deck + mode-entry MIDI signal)

New "HOTCUE" screen accessible from the main menu (magenta icon) with 8
hot cue trigger pads per deck, laid out as two rows of 4:

- **16 hot cue buttons** (8 per deck) colored to match Rekordbox's default
  pad colors (red, orange, yellow, green, cyan, blue, purple, pink).
- Each button sends a momentary Note On:
  - Deck 1: `NOTE_HOTCUE_D1_1`..`NOTE_HOTCUE_D1_8` (70-77)
  - Deck 2: `NOTE_HOTCUE_D2_1`..`NOTE_HOTCUE_D2_8` (78-85)
  Map each to the corresponding Hot Cue 1-8 in Rekordbox MIDI Learn.
- **Mode-entry signal**: entering the Hot Cue screen sends
  `NOTE_HOTCUE_MODE_ENTER` (86) so Rekordbox can be mapped to switch to
  Hot Cue performance pad mode automatically.
- Visual tap feedback: buttons briefly flash inverted on press.
- Menu icon grid resized from 52px/10px to 44px/8px icons to fit the 6th
  function button on the 320px display.

### Added - LED brightness control on the Setup screen

The Setup Home screen now has a scrollable layout with an LED section
at the bottom. Tap the down-arrow to scroll and reveal `[-]` / `[+]`
buttons that adjust the back LED brightness (0-100%, step 5%). The
chosen value is saved to NVS and applied immediately when Bluetooth is
connected. Default is 15%.

### Changed - Needle Search screen renamed to Song Search

Renamed the screen header from "NEEDLE SEARCH" to "SONG SEARCH" for clarity.

### Added - Cue point navigation buttons on Song Search screen

Added Previous Cue (`< CUE`) and Next Cue (`CUE >`) buttons for each deck on
the Song Search screen. Buttons sit below each needle strip and send momentary
Note On messages for mapping to Rekordbox's cue point navigation:

- Deck 1: `NOTE_CUE_PREV_D1` (42), `NOTE_CUE_NEXT_D1` (43)
- Deck 2: `NOTE_CUE_PREV_D2` (44), `NOTE_CUE_NEXT_D2` (45)

Map each to the corresponding "Prev Cue" / "Next Cue" function in Rekordbox
MIDI Learn. The needle strip height was reduced from 60px to 44px to
accommodate the new buttons without exceeding the 240px display height.

### Fixed - Wave Zoom sensitivity too high in RB View screen

The Wave Zoom slider sent a new CC message for every pixel of finger
movement (128 discrete values across the 280px strip), making the zoom
level change too aggressively in Rekordbox. The output is now quantized
to steps of 4 (`RBV_ZOOM_STEP`), yielding ~32 discrete zoom levels
instead of 128. The slider still covers the full visual range but the
MIDI values only update when the finger crosses a step boundary.

### Fixed - Needle Search position jumps too fast

The Needle Search strips sent a new CC on every single-pixel change,
causing rapid position jumps in Rekordbox from minor finger movement or
touchscreen jitter. Added a minimum change threshold (`NS_MIN_CHANGE = 3`)
so a new MIDI message is only sent when the value has moved at least 3
steps from the last sent position.

### Changed - Back LED uses PWM for dimmer Bluetooth indicator

Switched the RGB LED on the back of the CYD from `digitalWrite` (full
on/off) to PWM via `ledcAttach`/`ledcWrite`. The Bluetooth-connected
indicator now lights blue at ~16% brightness (`LED_DIM_BRIGHTNESS = 40`)
instead of full blast. The constant lives in `common_definitions.h` for
easy adjustment.

### Changed - Bluetooth/WiFi status icons redesigned as XBM bitmaps

Replaced the procedural line/arc drawing with pre-rendered XBM (monochrome
bitmap) icons stored in PROGMEM (`icons.h`), matching the standard
Bluetooth and WiFi logo shapes. Both icons are the same height (17px) so
they look proportionally balanced side by side in the header.

- **Bluetooth**: full bind-rune (ᚼ+ᛒ) with thick strokes, including the
  left Hagall cross lines that were missing before. 13x17 pixel XBM drawn
  standalone (removed the rounded-rect badge wrapper).
- **WiFi**: three thick filled arc bands (42-138 deg) fanning upward from
  a bottom point, matching the standard WiFi icon shape. 23x17 pixel XBM
  with clear gaps between bands.
- **Color-only state indication**: connected BT = blue (`BLUETOOTH_BLUE`),
  connected WiFi = green (`THEME_SUCCESS`), connecting WiFi = amber
  (`THEME_WARNING`), disconnected = dim gray (`THEME_TEXT_DIM`). Removed
  the filled/outline badge distinction.
- Both icons vertically centered at the same Y position for visual balance.
- `generate_icons.py` included for regenerating the XBM arrays if icon
  geometry needs to change.

### Added - Back RGB LED indicates Bluetooth connection status

The RGB LED on the back of the CYD board now lights up blue when a Bluetooth
device is connected and turns off when disconnected. Provides an at-a-glance
physical indicator of connection state without needing to look at the screen.

- LED pins: GPIO 4 (R), GPIO 16 (G), GPIO 17 (B) — active-LOW.
- `setBackLED()` helper added to main sketch for setting LED color.

### Changed - Effects: larger FX buttons, smaller paddle, orange active state

- FX buttons enlarged from 44x28 to 46x36 for easier finger tapping.
- Paddle shrunk from 100x120 to 86x100 to free space for the larger buttons.
- Paddle fills solid orange (0xFDA0) when active, with contrasting text so the
  ON state is immediately obvious.

### Fixed - Effects: multiple FX not activating when paddle turned on

When the paddle activated 2 or 3 armed FX simultaneously, the back-to-back
`setValue()`/`notify()` calls on the BLE characteristic were so fast that the
second write overwrote the first before it was transmitted. Rekordbox only
received one (or zero) of the messages. Added a 20ms delay between consecutive
MIDI sends in the paddle ON and paddle OFF loops so each note is delivered as a
separate BLE packet.

### Changed - Effects: multi-FX stacking per deck, paddle flipped to up=ON

- **Multi-FX stacking**: all three FX buttons (FX1, FX2, FX3) can be armed and
  activated independently on the same deck. Arm any combination while the paddle
  is off, then push the paddle to activate them all at once. Each FX toggles
  independently — turning one off while the paddle is active doesn't affect the
  others.
- **Paddle orientation flipped**: the knob now sits at the top when ON and at the
  bottom when OFF, matching the physical "push up to engage" feel.

### Changed - Effects screen: arm/activate FX model with paddle gating

Replaced the direct-toggle FX buttons and independent paddle MIDI note with a
three-state arm/activate model where the paddle gates when MIDI is actually sent:

- **Disarmed** (hollow): FX slot not selected.
- **Armed** (solid blue): FX slot selected, but paddle is OFF — no MIDI sent yet.
- **Active** (solid blue + flashing): FX slot armed AND paddle is ON — MIDI Note
  On was sent to Rekordbox when the paddle activated.

Behavior:
- Tapping an FX button when paddle is OFF toggles between Disarmed ↔ Armed (UI
  only, no MIDI).
- Pushing the paddle ON sends MIDI Note On for every armed FX (armed → active).
- Pushing the paddle OFF sends MIDI Note On for every active FX to turn them off
  in Rekordbox (active → armed). Disarmed buttons are unaffected.
- While paddle is ON, tapping an active FX disarms it and sends MIDI to turn it
  off; tapping a disarmed FX activates it immediately and sends MIDI to turn it on.
- Active FX buttons flash (~300ms interval) so the live state is visually distinct
  from merely armed.
- The paddle itself no longer sends its own dedicated MIDI note
  (`NOTE_FX_PADDLE_D1`/`NOTE_FX_PADDLE_D2` are unused now). Only the per-slot
  FX notes (30-35) are sent, gated by the paddle state.

### Added - Rekordbox View screen (panel toggles + wave zoom)

New "RB VIEW" screen accessible from the main menu (orange icon) for toggling
Rekordbox UI panels on/off and adjusting waveform zoom level:

- **4 panel toggle buttons** arranged in a 2x2 grid:
  - FX Panel (`NOTE_RBV_FX_PANEL` = 60)
  - Sampler Panel (`NOTE_RBV_SAMPLER_PANEL` = 61)
  - Mixer Panel (`NOTE_RBV_MIXER_PANEL` = 62)
  - Record Panel (`NOTE_RBV_RECORD_PANEL` = 63)
  Each sends a Note On toggle — map to the corresponding panel show/hide in
  Rekordbox MIDI Learn.
- **Wave Zoom slider**: a horizontal drag strip that sends CC 65 (0-127).
  Map to "WaveZoom" in Rekordbox MIDI Learn. Center position (64) is the
  default zoom level; drag left to zoom out, right to zoom in.
- Menu icon grid resized from 64px to 52px icons (with 10px spacing) to
  accommodate the 5th function button while staying centered on the 320px
  display.

## 2026-07-04

### Added - Vinyl toggle button on Deck Controls screen

Added a VINYL toggle button for each deck on the Deck Controls screen:

- New 4th row button per deck labeled "VINYL" using `THEME_SECONDARY` color.
- MIDI notes: `NOTE_D1_VINYL` (26) for deck 1, `NOTE_D2_VINYL` (27) for deck 2.
  Map each to the Vinyl Mode On/Off in Rekordbox MIDI Learn.
- Row height reduced from 48px to 38px and gap from 8px to 6px to fit all 4
  buttons within the 240px display height.

### Changed - Effects screen redesigned: per-deck FX buttons + paddle switch

Replaced the single rotary-knob FX selector with a two-column per-deck layout:

- **Each deck now has 3 FX toggle buttons** (FX1, FX2, FX3) that arm which
  effect slots are selected.
- **A large paddle switch per deck** activates/deactivates all armed FX slots
  at once. The paddle sends a toggle Note On for each selected FX when flipped
  down (active) or up (inactive).
- Removed the rotary knob (FX NEXT/PREV) and global FX ON/OFF / DECK ASSIGN
  buttons - the new per-deck model replaces all of those controls.
- MIDI notes updated: `NOTE_FX_D1_1`..`NOTE_FX_D1_3` (30-32) for deck 1,
  `NOTE_FX_D2_1`..`NOTE_FX_D2_3` (33-35) for deck 2. Map each to the
  corresponding FX slot On/Off in Rekordbox MIDI Learn.

### Changed - Stems SOLO now uses Rekordbox StemsMode function

Rekordbox has a native "StemsMode" function that switches between normal mute
and solo behavior internally. Instead of trying to replicate solo logic by
sending multiple Note On signals to other stems (which Rekordbox didn't
interpret correctly), the CYD now:

- Sends a dedicated **Note On for StemsMode** (notes 58/59 for deck 1/2) when
  the SOLO toggle is tapped — map this to "StemsMode" in Rekordbox MIDI Learn.
- Always sends a **single Note On for the tapped stem only**, regardless of
  whether SOLO mode is active. Rekordbox handles the "mute others" logic.
- UI is unchanged: in SOLO mode, tapping a stem visually highlights it (others
  turn red); tapping it again clears the highlight. This is cosmetic only.

### Fixed - Stems SOLO mode sending wrong MIDI signals

`applyStemSolo()` had complex "already soloed" detection logic that tried to
track and diff desired state. The actual behavior needed is much simpler:
tapping a stem in SOLO mode sends Note On to the OTHER 3 stems (not the
tapped one). Rekordbox toggles its own mute state per Note On, so first tap
mutes the others (isolating the tapped stem), second tap unmutes them.

- Replaced `applyStemSolo()` with a direct loop that sends `sendToggleNote()`
  to all stems except the tapped one and flips their local visual state.

### Fixed - Deck Controls (Master Tempo, Quantize, Slip) not triggering in Rekordbox

`sendToggleNote()` was sending Note On immediately followed by Note Off. This
caused two issues: (1) MIDI Learn captured the Note Off (0x80) instead of the
Note On (0x90) because it was the last message received, and (2) the Note Off
with velocity 0 wasn't recognized as a button press by Rekordbox.

- Changed `sendToggleNote()` to send **only Note On** (0x90, velocity 127).
  Rekordbox toggle-style functions (Quantize, Slip, Master Tempo, FX On/Off,
  stem mutes) flip their internal state on each Note On — Note Off is
  irrelevant for toggles and was only causing mapping confusion.

### Fixed - BLE crash on connect (xTaskPriorityDisinherit assertion failure)

The ESP32 rebooted with a FreeRTOS assertion crash every time macOS tried to
connect via Bluetooth. Root cause: the `onConnect`/`onDisconnect` BLE callbacks
ran in the NimBLE host task context but called SPI-heavy functions (`drawMenu()`,
`updateStatus()`) and `pCharacteristic->notify()` (via `sendMIDI()`), which
competed for FreeRTOS mutexes with the Arduino main loop task.

- Moved all TFT drawing, status updates, and advertising restart out of the
  BLE callbacks into deferred flags (`bleJustConnected`/`bleJustDisconnected`)
  that the main `loop()` picks up on each iteration.
- Removed the 128-note Note Off blast from `onDisconnect` — the client already
  disconnected so there's nobody to receive those messages.
- Added `pServer` global so the loop can call `pServer->startAdvertising()`
  after the 100ms teardown delay.

### Fixed - BLE reconnection failure on macOS (Audio MIDI Setup)

After disconnecting from Bluetooth via the Mac, the device could not be
reconnected in Audio MIDI Setup without first removing and re-discovering it.
Root cause: the BLE stack had no bonding/security configured, so macOS
discarded the pairing state on disconnect and couldn't recognize the device
as a known peer when it resumed advertising.

- Enabled NimBLE **bonding** (`BLE_SM_PAIR_AUTHREQ_BOND`) and set the I/O
  capability to "Just Works" (`BLE_HS_IO_NO_INPUT_OUTPUT`) so macOS stores
  and reuses the pairing keys across reconnections.
- Added `setScanFilter(NONE, NONE)` on the advertising object so previously
  bonded devices that attempt a direct reconnection (without an active scan)
  are accepted.
- Added a 100ms `delay()` before restarting advertising in `onDisconnect()`
  to let the NimBLE link-layer fully tear down the old connection; without
  this, macOS sometimes saw a stale connection state and refused to connect.
- Switched the disconnect callback to call `pServer->startAdvertising()`
  (server-scoped) instead of the global `BLEDevice::startAdvertising()` so
  the advertising parameters configured during setup are always reused.

### Fixed - Stems SOLO/mute logic was inverted and mode switch didn't reset

Three bugs in the stems solo/mute system:

- **All stems turned red when entering SOLO mode**: `isStemMuted()` inverted
  the `stemActive` flag in SOLO mode (`!stemActive`), so when all stems
  started at their default "unmuted" state, the inversion made them all
  display as muted the instant SOLO was toggled on.
- **SOLO sent MIDI mute to the wrong stem**: `applyStemSolo()` set
  `stemActive = true` (mute) on the *tapped* stem instead of the *other*
  three. The visual inversion in `isStemMuted()` masked this on-screen, but
  Rekordbox received the opposite of what was intended.
- **Mode switch carried stale state**: toggling between SOLO and NORMAL
  kept whatever stems were muted/soloed, producing confusing visuals and
  out-of-sync MIDI state.

Fixes:
- `isStemMuted()` now returns `stemActive` directly (no mode-dependent
  inversion) - `true` always means "muted in Rekordbox".
- `applyStemSolo()` now mutes all stems *except* the tapped one (`x != stem`),
  and un-solos by unmuting all when the already-soloed stem is tapped again.
- Toggling the SOLO slider first unmutes all four stems for that deck
  (via `setStemState(d, s, false)`) before flipping the mode flag, so both
  the UI and Rekordbox start clean.

### Fixed - Main menu/UI usability pass (spacing, icons, alignment, Setup placement, WiFi refresh, header overlap, Needle Search release, Stems SOLO/mute)

Addressed a round of hands-on usability issues found while using the device:

- **Main menu header spacing**: "DJ CONTROL" / "Bluetooth MIDI for Rekordbox"
  were crowding into each other with almost no vertical gap. Header grew
  from 50px to 54px tall and the title/subtitle baselines were repositioned
  (`drawMenu()` in `Rekordbox-Midi-Controller.ino`) to give them real
  breathing room.
- **Removed "v1.0" from the main menu** - it's now shown on the Setup Home
  screen instead (`setup_mode.h`, top-right of the header, via the new
  shared `APP_VERSION` constant in `common_definitions.h`), since a DJ
  mid-set doesn't need to see the firmware version.
- **Bluetooth glyph redesigned to match the requested reference icon**
  (`drawBluetoothGlyph()` in `ui_elements.h`): the rune's diagonals now
  correctly meet at the badge's center point (previously they crossed
  off-center, looking more like an hourglass/X than the real Bluetooth
  logo), and the badge is squarer instead of a tall pill. **Connected now
  fills with a dedicated brand blue (`BLUETOOTH_BLUE`)** instead of green,
  so it reads as "Bluetooth" at a glance instead of being confused with the
  WiFi icon's green; disconnected stays a plain hollow outline as
  requested.
- **Setup is no longer a main function icon.** It configures the device
  itself, not a Rekordbox control, so it doesn't belong alongside FX/
  DECKS/SEARCH/STEMS. It's now a small outline gear badge in the bottom
  right corner of the menu (`drawMenuGearButton()`/`MENU_GEAR_*` in
  `Rekordbox-Midi-Controller.ino`). The remaining 4 function icons grew
  (52px -> 64px) and were re-centered as a single balanced row in the
  freed-up space instead of hugging the top of the screen.
- **WiFi/Bluetooth status icons now update the instant the connection
  state actually changes**, instead of only catching up the next time the
  menu happened to redraw. Previously, connecting to WiFi while sitting on
  the main menu left the icon stuck grey/amber until you navigated away and
  back. `loop()` in `Rekordbox-Midi-Controller.ino` now tracks the last-
  drawn WiFi/BLE state and calls `drawStatusIcons()` as soon as it changes
  while on the menu.
- **Back chevron no longer overlaps screen titles.** `drawHeader()`
  (`ui_elements.h`) previously centered the title on the full screen width,
  so longer titles ("DECK CONTROLS", "NEEDLE SEARCH") had their left edge
  land underneath the back button. Titles are now left-aligned starting
  just past the button (`HEADER_TEXT_X`), and automatically drop to a
  smaller font (via `tft.textWidth()`) if they'd otherwise run past the
  right edge.
- **Needle Search no longer implies a phantom "recenter" MIDI command.**
  Lifting your finger off a strip now visually resets that deck's position
  marker back to the neutral center line (`handleNeedleStrip()` in
  `needle_search_mode.h`) so the strip never looks like it's still holding
  a stale position - but this reset is purely visual and sends **no** MIDI;
  only an actual touch-drag sends CC values.
- **Stems SOLO button was too small to hit reliably** (56x16px, well under
  a usable touch target). Enlarged to 84x30px and repositioned inline with
  the "DECK X" label instead of squeezed just above the stem row
  (`stems_mode.h`), which also freed up vertical rhythm across both deck
  rows.
- **Muted stems now turn solid red.** Added `isStemMuted()`/
  `drawStemButton()` (`stems_mode.h`): outside SOLO mode, a stem you've
  muted turns red; in SOLO mode, every stem *other than* the soloed one
  turns red, and the soloed stem keeps its normal deck-accent color -
  matching how the feature is actually used ("select Vocal to mute
  everything else" / "select Vocal to mute just the vocals").

### Changed - Bluetooth device name: "CYD MIDI" -> "RB-MIDI"

Renamed the BLE MIDI advertised device identifier from `CYD MIDI` to
`RB-MIDI` (`Rekordbox-Midi-Controller.ino`: `BLEDevice::init()`,
`BLEAdvertisementData::setName()`, and the boot log message). Updated the
on-device SETUP screen (`setup_mode.h`) and `README.md` pairing/MIDI Learn
instructions to match the new name.

### Removed - Swipe-to-go-back navigation

Removed the edge-swipe-back gesture (`updateSwipeBack()`, `SWIPE_EDGE_ZONE`/
`SWIPE_THRESHOLD`/`SWIPE_MAX_VERTICAL_DRIFT` in the main sketch) - it proved
unreliable to trigger consistently on this resistive touchscreen. The small
chevron (`‹`) back button in the header (`drawBackChevron()` in
`ui_elements.h`) is unchanged and is now the only way back to the main menu.
`README.md` updated accordingly.

### Added - Stems screen: per-deck SOLO mode

Added a small **SOLO** toggle button to each deck's row on the Stems screen
(`stems_mode.h`), top-right of the "DECK 1"/"DECK 2" label:

- **Off (default)**: tapping a stem behaves as before - just adds/removes
  that stem, independent of the others.
- **On**: tapping a stem now isolates it - it turns on while the other
  three are muted, for that deck only. Tapping the already-isolated stem
  again restores all four stems (mixer-style solo un-solo behavior).
- Since the device only sends momentary toggle notes and Rekordbox owns the
  authoritative on/off state, isolating a stem is implemented locally by
  comparing the desired end state against the mirrored `stemActive[][]`
  state and only firing a toggle note for the stems that actually need to
  change (`setStemState()`/`applyStemSolo()`), instead of blindly toggling
  all four every time.
- `README.md`: documented the new SOLO toggle behavior.

### Changed - Status icons: real Bluetooth/WiFi glyphs, solid = connected

Replaced the "B"/"W" letter badges in the main menu header with icons that
match the actual familiar Bluetooth/WiFi logos (`drawBluetoothGlyph()`/
`drawWifiGlyph()` in `ui_elements.h`):

- Bluetooth: the classic rune (vertical spine + two crossing diagonals)
  inside a rounded-square badge.
- WiFi: three concentric quarter-ring arcs fanning up from a dot, drawn
  with TFT_eSPI's `drawArc()`.
- **Solid/filled = connected; hollow outline = not connected** (same
  shape and size either way, just thicker/filled vs. thin/hollow - mirrors
  how solid vs. outline app icons usually indicate active state). WiFi
  additionally shows an amber outline while a connection attempt is in
  progress.

### Changed - Swipe-to-go-back navigation

Replaced the tap-only "BACK" pill button with an edge-swipe gesture, since
a precise tap target was one of the harder things to hit reliably on this
2.8" resistive touchscreen:

- On any non-menu screen, dragging left-to-right starting from near the
  left edge of the screen (`updateSwipeBack()` in the main sketch) now
  jumps straight back to the main menu, regardless of how deep the current
  screen is (e.g. from inside the WiFi password keyboard).
- The gesture only arms when the touch **starts** within a narrow strip at
  the very left edge (`SWIPE_EDGE_ZONE`), deliberately kept below the
  Needle Search strips' start position (`NS_STRIP_X = 20`) so it never
  hijacks a legitimate rightward drag on those strips.
- The old "BACK" pill was replaced with a small, quiet chevron (`‹`) icon
  in the same header position - still tappable (same hit region as
  before) as a fallback for when a swipe isn't picked up, but no longer a
  large, visually loud button since swiping is now the primary way back.

### Added - Dark/Light mode, bigger touch targets, Stems reorder

Reviewed every screen for touch-friendliness on the 2.8" resistive display
and made the following changes:

- Added a runtime-switchable **dark/light theme** (`theme_manager.h`):
  the original palette is now "dark mode", plus a new warm-paper "light
  mode" with deepened (not simply inverted) accent colors for legibility
  under bright venue lighting. Toggle lives on the Setup screen under a new
  "DISPLAY" section (DARK/LIGHT segmented buttons); the choice is saved to
  NVS and restored on boot, and applies instantly to every screen since all
  `THEME_*` colors are now runtime variables instead of compile-time
  constants.
- Enlarged the most error-prone touch targets:
  - The **BACK** button (present on every screen) grew from 50x25 to
    64x34 and its geometry was centralized into shared `BACK_BTN_*`
    constants (`ui_elements.h`) instead of being repeated as magic numbers
    in six different files.
  - Main menu app icons: 48x48/16px gaps -> 52x52/12px gaps.
  - Setup screen: WiFi scan result rows (24px -> 28px tall), on-screen
    keyboard keys (30px -> 33px tall), and the Setup Home action buttons
    (RESTART, SCAN NETWORKS, FORGET NETWORK) all grew slightly with more
    breathing room between them.
  - Effects screen: FX ON/OFF button grew from 140x26 to 150x28.
- **Stems screen**: reordered and relabeled the four toggles per-deck to
  **Vocal, Melody, Bass, Drums** (previously Vocal, Drums, Bass, Other).
  The underlying MIDI note numbers are unchanged (only `NOTE_STEM_D*_OTHER`
  was renamed to `NOTE_STEM_D*_MELODY` for clarity), so existing Rekordbox
  MIDI Learn mappings still work - only the on-screen order/label changed.
- `README.md`: documented the new DISPLAY toggle and updated the Stems
  mapping table for the new order/labels.

### Fixed - Sketch too big to fit flash (text section exceeds available space)

Switched the BLE MIDI stack from the stock ESP32 Bluedroid BLE library
(`BLEDevice.h`/`BLEServer.h`/`BLEUtils.h`/`BLE2902.h`) to `NimBLE-Arduino`,
which uses the same class names (via compatibility `#define`s) but has a
much smaller flash footprint. This was needed because the sketch had grown
to 1,710,451 bytes (130%) against the 1,310,720-byte app partition limit on
`ESP32 Dev Module`.

- `common_definitions.h` / `Rekordbox-Midi-Controller.ino`: replaced BLE
  includes with `<NimBLEDevice.h>`.
- Removed the manual `BLE2902` descriptor - NimBLE auto-creates the 0x2902
  (CCCD) descriptor for NOTIFY-capable characteristics.
- Replaced `BLECharacteristic::PROPERTY_*` with `NIMBLE_PROPERTY::*`.
- Replaced `advertising->setScanResponse()`/`setMinPreferred()`/
  `setMaxPreferred()` with NimBLE's `enableScanResponse()`/
  `setPreferredParams()`.
- Updated `MIDICallbacks::onConnect`/`onDisconnect` to NimBLE's callback
  signature (`NimBLEConnInfo&` parameter) and added `override` so a future
  API mismatch fails to compile instead of silently never firing.
- `README.md`: added `NimBLE-Arduino` to the library install list and a
  troubleshooting entry for flash-size errors (including the Partition
  Scheme fallback).

### Changed - DJ Controller Screen Redesign

Replaced the 10 generic music-making modes (KEYS, BEATS, ZEN, DROP, RNG, XY PAD, ARP, GRID, CHORD, LFO) with a focused 5-screen DJ control surface for Rekordbox Performance Mode, since the original plan to use the CYD as a live waveform display isn't possible without Pro DJ Link hardware support.

- Added **Effects** screen: a touch "rotary knob" gesture that cycles Beat FX (NEXT/PREV), plus FX ON/OFF and independent Deck 1/Deck 2 assign toggles.
- Added **Deck Controls** screen: Master Tempo, Quantize, and Slip Mode toggles for Deck 1 and Deck 2.
- Added **Needle Search** screen: two draggable position strips (one per deck) sending absolute CC position.
- Added **Stems** screen: Vocal/Drums/Bass/Other toggle grid for Deck 1 and Deck 2.
- Added a functional **WiFi setup flow** on the Setup screen: network scan, on-screen QWERTY keyboard for password entry, and credential persistence (NVS) with auto-reconnect on boot. WiFi has no data feature yet - this only establishes connectivity for future use.
- Added small Bluetooth/WiFi status badges to the main menu header.
- Added a documented MIDI note/CC mapping table (see `README.md`) so every new control can be mapped in Rekordbox via MIDI Learn.
- Removed all 10 old mode files and their now-unused helpers (musical scale tables, note-name conversion).
