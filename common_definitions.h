#ifndef COMMON_DEFINITIONS_H
#define COMMON_DEFINITIONS_H

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
// NimBLE-Arduino (h2zero) instead of the stock Bluedroid BLE stack - the
// Bluedroid stack alone pushes this sketch well past the 1.25MB app
// partition on ESP32 Dev Module. NimBLE aliases the classic BLEDevice/
// BLEServer/BLECharacteristic class names via #define, so the rest of the
// codebase is unchanged.
#include <NimBLEDevice.h>

// Color scheme - runtime variables (not compile-time constants) so the
// Setup screen can toggle between dark/light mode. Values are assigned by
// theme_manager.h; see that file for the actual dark/light palettes.
extern bool darkMode;
extern uint16_t THEME_BG;
extern uint16_t THEME_SURFACE;
extern uint16_t THEME_PRIMARY;
extern uint16_t THEME_SECONDARY;
extern uint16_t THEME_ACCENT;
extern uint16_t THEME_SUCCESS;
extern uint16_t THEME_WARNING;
extern uint16_t THEME_ERROR;
extern uint16_t THEME_TEXT;
extern uint16_t THEME_TEXT_DIM;

// App version - shown on the Setup screen only (kept off the main menu so
// the home screen stays focused on the four controller functions).
#define APP_VERSION "v1.0"

// Bluetooth brand blue for the connected status badge - deliberately not
// THEME_SUCCESS (green), since a blue-filled Bluetooth glyph is the
// recognizable "connected" signal regardless of light/dark theme.
#define BLUETOOTH_BLUE 0x2BDF

// RGB LED on the back of the CYD board (active-LOW: LOW = on, HIGH = off)
#define LED_R_PIN 4
#define LED_G_PIN 16
#define LED_B_PIN 17

// BLE MIDI UUIDs
#define SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

// --- MIDI mapping ---
// Map every control below to a Rekordbox Performance Mode function using
// Preferences > MIDI > MIDI Learn. Values are arbitrary but distinct - only
// the note/CC number matters, not what it "means" in General MIDI terms.

// Deck Controls screen - Note On/Off, channel 1
#define NOTE_D1_MASTER_TEMPO 20
#define NOTE_D1_QUANTIZE     21
#define NOTE_D1_SLIP         22
#define NOTE_D2_MASTER_TEMPO 23
#define NOTE_D2_QUANTIZE     24
#define NOTE_D2_SLIP         25
#define NOTE_D1_VINYL        26
#define NOTE_D2_VINYL        27

// Effects screen - Note On/Off
// Per-deck FX slot toggles: FX1/FX2/FX3 for each deck, plus an
// independent paddle switch per deck that sends its own MIDI note.
#define NOTE_FX_D1_1 30
#define NOTE_FX_D1_2 31
#define NOTE_FX_D1_3 32
#define NOTE_FX_D2_1 33
#define NOTE_FX_D2_2 34
#define NOTE_FX_D2_3 35
#define NOTE_FX_PADDLE_D1 36
#define NOTE_FX_PADDLE_D2 37

// Needle Search screen - CC, absolute value 0-127 (maps to "NeedleSearch")
#define CC_NEEDLE_D1 40
#define CC_NEEDLE_D2 41

// Stems screen - Note On/Off toggles (Vocal, Melody, Bass, Drums) x2 decks.
// Note values are unchanged from earlier revisions (only the on-screen
// order/labels changed) so existing Rekordbox MIDI Learn mappings still work.
#define NOTE_STEM_D1_VOCAL  50
#define NOTE_STEM_D1_DRUMS  51
#define NOTE_STEM_D1_BASS   52
#define NOTE_STEM_D1_MELODY 53
#define NOTE_STEM_D2_VOCAL  54
#define NOTE_STEM_D2_DRUMS  55
#define NOTE_STEM_D2_BASS   56
#define NOTE_STEM_D2_MELODY 57

// StemsMode - toggles Rekordbox between normal stem mute and solo behavior.
// Global (not per-deck). Map to "ActiveStem Mute/Solo" in Rekordbox MIDI settings.
#define NOTE_STEMS_MODE_D1  58

// Rekordbox View screen - Note On toggles for panel visibility.
// Map each to the corresponding Panel On/Off in Rekordbox MIDI Learn.
#define NOTE_RBV_FX_PANEL      60
#define NOTE_RBV_SAMPLER_PANEL 61
#define NOTE_RBV_MIXER_PANEL   62
#define NOTE_RBV_RECORD_PANEL  63

// Wave Zoom - CC, absolute value 0-127 (maps to "WaveZoom" in Rekordbox)
#define CC_RBV_WAVE_ZOOM       65

// Touch handling
struct TouchState {
  bool wasPressed = false;
  bool isPressed = false;
  bool justPressed = false;
  bool justReleased = false;
  int x = 0, y = 0;
};

// App modes
enum AppMode {
  MENU,
  EFFECTS,
  DECK_CONTROLS,
  NEEDLE_SEARCH,
  STEMS,
  RB_VIEW,
  SETUP
};

// Global objects - declared in main file
extern TFT_eSPI tft;
extern XPT2046_Touchscreen ts;
extern BLECharacteristic *pCharacteristic;
extern bool deviceConnected;
extern uint8_t midiPacket[];
extern TouchState touch;
extern AppMode currentMode;

// WiFi state - updated by wifi_manager.h, read by ui_elements.h/setup_mode.h
extern bool wifiConnected;
extern bool wifiConnecting;
extern String currentSSID;

#endif
