# CYD DJ MIDI Controller

Touchscreen Bluetooth MIDI controller for the ESP32-2432S028R "Cheap Yellow Display" (CYD), purpose-built as a companion controller for **Rekordbox Performance Mode**.

Special thanks to Brian Lough for putting together the resources on this board. Check out his repo for more examples: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

## Why this exists

This started as an attempt to use the CYD as a live waveform display for a DDJ-REV5, but that hardware doesn't support Pro DJ Link. Instead, the CYD is now a small wireless (Bluetooth MIDI) control surface that sits next to the DDJ-REV5 and sends extra controls into Rekordbox that the hardware controller doesn't expose.

## Screens

Every screen (except the main menu) can be exited via the small chevron (`‹`) in the top-left corner - it jumps straight back to the main menu, no matter how deep you are (e.g. from the WiFi password keyboard).

- **Main Menu** - jump to any screen below. Shows small Bluetooth/WiFi icon badges top-right: solid green = connected, outlined gray = off, outlined amber = WiFi connecting.
- **FX (Effects)** - a touch "knob" you rotate with your finger to cycle through Beat FX, plus an ON/OFF toggle and independent Deck 1 / Deck 2 assign toggles.
- **DECKS (Deck Controls)** - Master Tempo, Quantize, and Slip Mode toggles for Deck 1 and Deck 2.
- **SEARCH (Needle Search)** - two touch strips (one per deck) - drag across a strip to jump to that position in the track.
- **STEMS** - Vocal / Melody / Bass / Drums stem toggles for Deck 1 and Deck 2, plus a small **SOLO** toggle per deck: off, tapping a stem just adds/removes it; on, tapping a stem isolates it (mutes the other three), and tapping the isolated stem again restores all four.
- **VIEW (RB View)** - toggle Rekordbox UI panels (FX, Sampler, Mixer, Record) on/off, plus a horizontal Wave Zoom slider (CC 0-127).
- **HOTCUE (Hot Cue)** - 8 hot cue trigger pads per deck (2 rows of 4), color-coded to match Rekordbox's default pad colors. Buttons are momentary triggers (not toggles). Entering this screen sends two MIDI signals so you can map Rekordbox to auto-switch to Hot Cue pad mode and/or change the screen view.
- **SETUP** - Bluetooth status + restart advertising, WiFi scan/connect with an on-screen keyboard for entering the password (credentials saved on-device and reconnected automatically on boot), and a **DARK/LIGHT** display toggle (saved on-device, applies instantly to every screen).

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
| Effects | Knob clockwise tick | Note 30 | `FX1-1Select` (move up FX list) |
| Effects | Knob counter-clockwise tick | Note 31 | `FX1-1Select` set to type `Rotary`/`.Down` (see note below) |
| Effects | FX ON/OFF | Note 32 | `FX1-1On` |
| Effects | Deck 1 assign | Note 33 | `FX1Assign.LEFT` |
| Effects | Deck 2 assign | Note 34 | `FX1Assign.RIGHT` |
| Needle Search | Deck 1 strip | CC 40 (0-127) | Deck 1 `NeedleSearch` |
| Needle Search | Deck 2 strip | CC 41 (0-127) | Deck 2 `NeedleSearch` |
| Stems | Deck 1 Vocal / Melody / Bass / Drums | Notes 50, 53, 52, 51 | Deck 1 stem mute/isolate functions |
| Stems | Deck 2 Vocal / Melody / Bass / Drums | Notes 54, 57, 56, 55 | Deck 2 stem mute/isolate functions |
| Stems | SOLO toggle | Note 58 | `StemsMode` (Mute/Solo switch) |
| RB View | FX Panel | Note 60 | FX Panel On/Off |
| RB View | Sampler Panel | Note 61 | Sampler Panel On/Off |
| RB View | Mixer Panel | Note 62 | Mixer Panel On/Off |
| RB View | Record Panel | Note 63 | Record Panel On/Off |
| RB View | Wave Zoom | CC 65 (0-127) | `WaveZoom` |
| Hot Cue | Deck 1 Hot Cue 1-8 | Notes 70-77 | Deck 1 `HotCue 1`-`HotCue 8` |
| Hot Cue | Deck 2 Hot Cue 1-8 | Notes 78-85 | Deck 2 `HotCue 1`-`HotCue 8` |
| Hot Cue | Mode enter (on screen open) | Note 86 | Hot Cue pad mode switch |
| Hot Cue | View switch (on screen open) | Note 87 | Screen view switch |

### About the FX knob direction

Rekordbox's stock MIDI Learn UI only lets you map a button to move **up** the FX list (`FX1-1Select`). Moving down requires manually editing the MIDI mapping you export from Rekordbox (a CSV/XML file) and adding a second row for the same function with `.Down` appended, or changing its `Type` column to `Rotary` so Rekordbox treats the CC as a relative encoder. This is optional - clockwise rotation works immediately with the default mapping.

## What You Need

- **ESP32-2432S028R (CYD)** - ~$15 from AliExpress/Amazon
- Arduino IDE with ESP32 support

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

(WiFi and Preferences are part of the ESP32 core - no extra install needed.)

> **Why NimBLE?** The sketch uses NimBLE-Arduino instead of the stock ESP32
> Bluedroid BLE stack (`BLEDevice.h`/`BLEServer.h`/etc. from the ESP32 core)
> because Bluedroid alone is large enough to overflow the default 1.25MB app
> partition once WiFi and TFT_eSPI are also linked in. NimBLE is a drop-in
> replacement (same class names) with a much smaller flash footprint.

### 3. Configure TFT_eSPI
Replace the `libraries/TFT_eSPI/User_Setup.h` with the `User_Setup.h` from the repo.

### 4. Upload Code
1. Clone this repo and open `Rekordbox-Midi-Controller.ino`
2. Select board: `ESP32 Dev Module`
3. Connect CYD and upload
(Lower Upload Speed to `115200` if the sketch isn't uploading)

### 5. Connect
1. Pair "RB-MIDI" via Bluetooth (macOS: System Settings > Bluetooth)
2. Select "RB-MIDI" as a MIDI input in Rekordbox / Audio MIDI Setup
3. Open the SETUP screen on the device to connect it to your WiFi network (optional, for future use)
4. Map each control in Rekordbox's MIDI Learn as described above

## Troubleshooting

- **Upload Speed**: Lower it to `115200` if the sketch isn't uploading
- **Blank screen**: Check TFT_eSPI pin configuration
- **No touch**: Verify touchscreen library installation
- **No Bluetooth**: Restart device and re-pair, or use the "RESTART" button on the Setup screen
- **WiFi won't connect**: Use "FORGET NETWORK" on the Setup screen and try scanning again
- **"Sketch too big" / "text section exceeds available space"**: Make sure `NimBLE-Arduino` is installed and the includes are `<NimBLEDevice.h>` (not the stock `<BLEDevice.h>`/`<BLEServer.h>`/`<BLE2902.h>`) - the stock Bluedroid BLE stack alone is large enough to overflow the default partition. If it still doesn't fit, go to `Tools` → `Partition Scheme` and pick one with more app space (e.g. `Minimal SPIFFS (1.9MB APP with OTA)` or `No OTA (2MB APP/2MB SPIFFS)`).

## License

Open source - see MIT license file for details.
