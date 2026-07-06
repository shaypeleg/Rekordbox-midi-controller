# CYD DJ MIDI Controller

Touchscreen Bluetooth MIDI controller for the ESP32-2432S028R "Cheap Yellow Display" (CYD), purpose-built as a companion controller for **Rekordbox Performance Mode**.

Special thanks to Brian Lough for putting together the resources on this board. Check out his repo for more examples: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

## Why this exists

This started as an attempt to use the CYD as a live waveform display for a DDJ-REV5, but that hardware doesn't support Pro DJ Link. Instead, the CYD is now a small wireless (Bluetooth MIDI) control surface that sits next to the DDJ-REV5 and sends extra controls into Rekordbox that the hardware controller doesn't expose — plus a real-time track info display powered by a companion Python server.

## Screens

Every screen (except the main menu) can be exited via the small chevron (`‹`) in the top-left corner - it jumps straight back to the main menu, no matter how deep you are (e.g. from the WiFi password keyboard).

- **Main Menu** - jump to any screen below. Two rows of icons: Row 1 (FX, Decks, HotCue, Stems) for controller functions, Row 2 (Track, Scroll, Views) for display/navigation. Small Bluetooth/WiFi icon badges top-right: blue = BT connected, green = WiFi connected, amber = WiFi connecting, dim gray = off. Setup gear icon in the bottom-right corner.
- **FX (Effects)** - per-deck FX control with 3 FX slot buttons (FX1/FX2/FX3) and a paddle switch per deck. Tap buttons to arm FX slots, push the paddle to activate all armed slots at once (sends MIDI), pull paddle back to deactivate. Active FX flash to distinguish from merely armed.
- **DECKS (Deck Controls)** - Master Tempo, Quantize, Slip Mode, and Vinyl toggles for Deck 1 and Deck 2.
- **SCROLL (Song Search)** - two touch strips (one per deck) for needle search position, plus Previous Cue / Next Cue buttons below each strip.
- **STEMS** - Vocal / Melody / Bass / Drums stem toggles for Deck 1 and Deck 2, plus a **SOLO** toggle per deck: off, tapping a stem just adds/removes it; on, tapping a stem isolates it (mutes the other three), and tapping the isolated stem again restores all four.
- **VIEWS (RB View)** - toggle Rekordbox UI panels (FX, Sampler, Mixer, Record) on/off, plus a horizontal Wave Zoom slider (CC 0-127).
- **HOTCUE (Hot Cue)** - 8 hot cue trigger pads per deck (2 rows of 4), color-coded to match Rekordbox's default pad colors. Buttons are gated (Note On when pressed, Note Off when released) matching DDJ-REV5 behavior. Entering this screen sends two MIDI signals so you can map Rekordbox to auto-switch to Hot Cue pad mode and/or change the screen view.
- **TRACK (Track Info)** - real-time now-playing display showing per-deck title, BPM, key, duration, comment, color waveform with hot cue markers and legend. Requires the companion Python server on the same network. Two view modes: compact (both decks at once) and expanded (tap a deck for full-screen detail with scrolling title marquee). Swap A/B button to correct deck assignment.
- **SETUP** - Bluetooth status + restart advertising, WiFi scan/connect with an on-screen keyboard for entering the password (credentials saved on-device and reconnected automatically on boot), LED brightness control, and a **DARK/LIGHT** display toggle (saved on-device, applies instantly to every screen).

## Mapping controls in Rekordbox

This device is a **generic BLE MIDI controller** - it doesn't ship with a Rekordbox device preset. After connecting, open Rekordbox: `Preferences > MIDI > MIDI Learn`, select "RB-MIDI", and map each control below to the Rekordbox function you want (click the function, click `LEARN`, then tap the matching control on the CYD).

| Screen | Control | MIDI message | Suggested Rekordbox function |
| --- | --- | --- | --- |
| Deck Controls | Deck 1 Master Tempo | Note 20 | Deck 1 `MasterTempo` |
| Deck Controls | Deck 1 Quantize | Note 21 | Deck 1 `Quantize` |
| Deck Controls | Deck 1 Slip Mode | Note 22 | Deck 1 `Slip` |
| Deck Controls | Deck 2 Master Tempo | Note 23 | Deck 2 `MasterTempo` |
| Deck Controls | Deck 2 Quantize | Note 24 | Deck 2 `Quantize` |
| Deck Controls | Deck 2 Slip Mode | Note 25 | Deck 2 `Slip` |
| Deck Controls | Deck 1 Vinyl | Note 26 | Deck 1 `Vinyl` |
| Deck Controls | Deck 2 Vinyl | Note 27 | Deck 2 `Vinyl` |
| Effects | Deck 1 FX1 / FX2 / FX3 | Notes 30, 31, 32 | `FX1-1On`, `FX1-2On`, `FX1-3On` |
| Effects | Deck 2 FX1 / FX2 / FX3 | Notes 33, 34, 35 | `FX2-1On`, `FX2-2On`, `FX2-3On` |
| Song Search | Deck 1 strip | CC 40 (0-127) | Deck 1 `NeedleSearch` |
| Song Search | Deck 2 strip | CC 41 (0-127) | Deck 2 `NeedleSearch` |
| Song Search | Deck 1 Prev Cue / Next Cue | Notes 42, 43 | Deck 1 `PrevCue` / `NextCue` |
| Song Search | Deck 2 Prev Cue / Next Cue | Notes 44, 45 | Deck 2 `PrevCue` / `NextCue` |
| Stems | Deck 1 Vocal / Drums / Bass / Melody | Notes 50, 51, 52, 53 | Deck 1 stem mute functions |
| Stems | Deck 2 Vocal / Drums / Bass / Melody | Notes 54, 55, 56, 57 | Deck 2 stem mute functions |
| Stems | SOLO toggle | Note 58 | `ActiveStem Mute/Solo` |
| RB View | FX Panel | Note 60 | FX Panel On/Off |
| RB View | Sampler Panel | Note 61 | Sampler Panel On/Off |
| RB View | Mixer Panel | Note 62 | Mixer Panel On/Off |
| RB View | Record Panel | Note 63 | Record Panel On/Off |
| RB View | Wave Zoom | CC 65 (0-127) | `WaveZoom` |
| Hot Cue | Deck 1 Hot Cue 1-8 | Notes 70-77 | Deck 1 `HotCue 1`-`HotCue 8` |
| Hot Cue | Deck 2 Hot Cue 1-8 | Notes 78-85 | Deck 2 `HotCue 1`-`HotCue 8` |
| Hot Cue | Mode enter (on screen open) | Note 86 | Hot Cue pad mode switch |
| Hot Cue | View switch (on screen open) | Note 87 | Screen view switch |

## What You Need

- **ESP32-2432S028R (CYD)** - ~$15 from AliExpress/Amazon
- Arduino IDE with ESP32 support
- Python 3.10+ (for the companion Track Info server, optional)

## Installation

### 1. Add ESP32 Board Support
1. Go to `File` → `Preferences`
2. Add to "Additional Boards Manager URLs":
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Go to `Tools` → `Board` → `Boards Manager`
4. Search "ESP32" and install "ESP32 by Espressif Systems"

### 2. Install Libraries
In Arduino IDE Library Manager, install:
- `TFT_eSPI` by Bodmer
- `XPT2046_Touchscreen` by Paul Stoffregen
- `NimBLE-Arduino` by h2zero
- `ArduinoWebsockets` by Gil Maimon
- `ArduinoJson` by Benoit Blanchon

(WiFi, ESPmDNS, and Preferences are part of the ESP32 core - no extra install needed.)

> **Why NimBLE?** The sketch uses NimBLE-Arduino instead of the stock ESP32
> Bluedroid BLE stack (`BLEDevice.h`/`BLEServer.h`/etc. from the ESP32 core)
> because Bluedroid alone is large enough to overflow the default 1.25MB app
> partition once WiFi and TFT_eSPI are also linked in. NimBLE is a drop-in
> replacement (same class names) with a much smaller flash footprint.

### 3. Configure TFT_eSPI
Replace the `libraries/TFT_eSPI/User_Setup.h` with the `User_Setup.h` from the repo.

### 4. Set Partition Scheme
Go to `Tools` → `Partition Scheme` and select **"Huge APP (3MB No OTA / 1MB SPIFFS)"**. The default 1.2MB partition is too small for this sketch with WiFi + BLE + WebSocket + TFT libraries.

### 5. Upload Code
1. Clone this repo and open `Rekordbox-Midi-Controller.ino`
2. Select board: `ESP32 Dev Module`
3. Connect CYD and upload
(Lower Upload Speed to `115200` if the sketch isn't uploading)

### 6. Connect
1. Pair "RB-MIDI" via Bluetooth (macOS: System Settings > Bluetooth)
2. Select "RB-MIDI" as a MIDI input in Rekordbox / Audio MIDI Setup
3. Open the SETUP screen on the device to connect it to your WiFi network (required for Track Info)
4. Map each control in Rekordbox's MIDI Learn as described above

## Companion App (Track Info Server)

The **Track Info** screen requires a Python server running on the same machine as Rekordbox. The server monitors Rekordbox's open audio files via `lsof`, reads metadata/waveform/cue data from Rekordbox's analysis files using `pyrekordbox`, and pushes updates to the CYD over WebSocket.

### Quick Start

```bash
./run.sh
```

Or manually:

```bash
cd companion_app
pip install -r requirements.txt
python3 nowplaying_server.py
```

### How It Works

1. The server polls `lsof` to detect which audio files Rekordbox currently has open
2. New file descriptors are tracked to determine which deck loaded which track
3. Metadata (title, BPM, key, cues, waveform) is read from Rekordbox's ANLZ files via `pyrekordbox`
4. Data is pushed to all connected CYD clients over WebSocket (port 9100)
5. The CYD discovers the server automatically via mDNS (`_rekordbox-cyd._tcp.local`) - zero configuration

### Requirements

- Python 3.10+
- macOS (uses `lsof` for file descriptor monitoring)
- Rekordbox must be running with tracks loaded
- CYD and server must be on the same WiFi network

### Dependencies

- `pyrekordbox` - reads Rekordbox database and analysis files
- `websockets` - WebSocket server
- `zeroconf` - mDNS service advertisement for auto-discovery

## Troubleshooting

- **Upload Speed**: Lower it to `115200` if the sketch isn't uploading
- **"Sketch too big" / "text section exceeds available space"**: Make sure the partition scheme is set to "Huge APP (3MB No OTA / 1MB SPIFFS)" under `Tools` → `Partition Scheme`. Also verify `NimBLE-Arduino` is installed and the includes are `<NimBLEDevice.h>` (not the stock `<BLEDevice.h>`).
- **Blank screen**: Check TFT_eSPI pin configuration
- **No touch**: Verify touchscreen library installation
- **No Bluetooth**: Restart device and re-pair, or use the "RESTART" button on the Setup screen
- **WiFi won't connect**: Use "FORGET NETWORK" on the Setup screen and try scanning again
- **Track Info shows "Searching..."**: Ensure the companion server is running, both devices are on the same WiFi, and mDNS is not blocked by your router
- **Track Info wrong deck assignment**: Tap the swap (↑↓) button in the top-right corner. The server's initial deck guess may be wrong when starting with cached files - it self-corrects on the first track load

## License

Open source - see MIT license file for details.
