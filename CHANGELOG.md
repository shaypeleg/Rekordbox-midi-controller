# Changelog

All notable changes to this project are documented in this file.

## 2026-07-05

### Changed - Effects: single FX per deck, paddle flipped to up=ON

- **Single FX selection**: only one FX button can be armed or active per deck at
  a time. Selecting a different FX automatically disarms/deactivates the previous
  one (with MIDI sent if the paddle is active).
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
