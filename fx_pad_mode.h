#ifndef FX_PAD_MODE_H
#define FX_PAD_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Combo FX pad workflow (Rekordbox MIDI Learn constraints):
//   1. SETUP - arm FX1/FX2/FX3 per deck (same notes as Effects screen),
//      cycle CFX with Next/Back, then DECK1 or DECK2 (one deck)
//   2. PAD   - finger down toggles that deck's armed FX On + drives X/Y CCs;
//              finger up toggles FX Off, Level->0, CFX param->64
// MAP screen kept in code for future MIDI Learn; hidden from UI (FXPAD_SHOW_MAP).

enum FxPadScreen { FXPAD_SETUP, FXPAD_PAD, FXPAD_MAP };

// Set to 1 to show the TEMP MAP button again for MIDI Learn helpers.
#define FXPAD_SHOW_MAP 0

// Same MIDI notes as effects_mode.h (FX1-1On … FX2-3On → 901E-9023)
const byte fxPadD1Notes[3] = {NOTE_FX_D1_1, NOTE_FX_D1_2, NOTE_FX_D1_3};
const byte fxPadD2Notes[3] = {NOTE_FX_D2_1, NOTE_FX_D2_2, NOTE_FX_D2_3};

// Setup layout — both decks' FX1/2/3 + CFX cycle
#define FXPAD_COL_L_X 8
#define FXPAD_COL_R_X 168
#define FXPAD_COL_W 144
#define FXPAD_FX_BTN_W 42
#define FXPAD_FX_BTN_H 36
#define FXPAD_FX_BTN_GAP 4
#define FXPAD_FX_BTN_Y (CONTENT_Y + 22)

// Extra gap below FX1/2/3 so COLOR FX label + buttons sit lower
#define FXPAD_CFX_Y (FXPAD_FX_BTN_Y + FXPAD_FX_BTN_H + 36)
#define FXPAD_CFX_BTN_W 100
#define FXPAD_CFX_BTN_H 36
#define FXPAD_CFX_PREV_X 20
#define FXPAD_CFX_NEXT_X (320 - FXPAD_CFX_BTN_W - 20)

#define FXPAD_START_W 110
#define FXPAD_START_H 36
#define FXPAD_MAP_W 56
#define FXPAD_MAP_H 34
#define FXPAD_START_Y (240 - FXPAD_START_H - 10)
#define FXPAD_BTN_GAP 12
#if FXPAD_SHOW_MAP
#define FXPAD_START1_X ((320 - (FXPAD_START_W * 2 + FXPAD_MAP_W + FXPAD_BTN_GAP * 2)) / 2)
#define FXPAD_START2_X (FXPAD_START1_X + FXPAD_START_W + FXPAD_BTN_GAP)
#define FXPAD_MAP_BTN_X (FXPAD_START2_X + FXPAD_START_W + FXPAD_BTN_GAP)
#define FXPAD_MAP_BTN_Y FXPAD_START_Y
#else
#define FXPAD_START1_X ((320 - (FXPAD_START_W * 2 + FXPAD_BTN_GAP)) / 2)
#define FXPAD_START2_X (FXPAD_START1_X + FXPAD_START_W + FXPAD_BTN_GAP)
#define FXPAD_MAP_BTN_X 0
#define FXPAD_MAP_BTN_Y 0
#endif

// TEMP MIDI Learn map-screen geometry — D1/D2 pick which deck's 3 Level
// sliders + CFX Y are shown (one axis at a time for Learn).
#define FXPAD_LEARN_DECK_Y (CONTENT_Y)
#define FXPAD_LEARN_Y_X 16
#define FXPAD_LEARN_Y_Y (CONTENT_Y + 36)
#define FXPAD_LEARN_Y_W 44
#define FXPAD_LEARN_Y_H 100
#define FXPAD_LEVEL_X 72
#define FXPAD_LEVEL_W 236
#define FXPAD_LEVEL_H 28
#define FXPAD_LEVEL_GAP 6
#define FXPAD_LEVEL_Y0 (CONTENT_Y + 36)
#define FXPAD_LEARN_GATE_X 72
#define FXPAD_LEARN_GATE_Y (FXPAD_LEVEL_Y0 + 3 * (FXPAD_LEVEL_H + FXPAD_LEVEL_GAP) + 8)
#define FXPAD_LEARN_GATE_W 90
#define FXPAD_LEARN_GATE_H 32
#define FXPAD_DECK_W 54
#define FXPAD_DECK_H 26
#define FXPAD_DECK1_X 56
#define FXPAD_DECK2_X 118

#define FXPAD_PAD_BG      0x1082
#define FXPAD_CROSS_COLOR 0x4A69
#define FXPAD_CURSOR_COLOR 0x07FF
#define FXPAD_MIN_CHANGE 3
#define FXPAD_COLOR_CENTER 64
// Reason: NimBLE notify() can drop back-to-back packets on one characteristic.
// Effects paddle uses 20ms; pad also fires Level CCs right after On notes, so
// use a larger gap and always trail the last Note so the CC cannot clobber it.
#define FXPAD_MIDI_GAP_MS 40
#define FXPAD_GLOW_R_MAX  24
#define FXPAD_GLOW_FRAME_MS 45
#define FXPAD_TRAIL_LEN    16
#define FXPAD_TRAIL_LIFE   10
#define FXPAD_TRAIL_MIN_DIST 6
// Block size for the djay-style pixelated glow (larger = chunkier pixels)
#define FXPAD_PIXEL_SIZE   4

// Per-slot LevelDepth CCs — same order as FX1/FX2/FX3 buttons
const byte fxPadLevelCcD1[3] = {CC_FX_LEVEL_D1_1, CC_FX_LEVEL_D1_2, CC_FX_LEVEL_D1_3};
const byte fxPadLevelCcD2[3] = {CC_FX_LEVEL_D2_1, CC_FX_LEVEL_D2_2, CC_FX_LEVEL_D2_3};

FxPadScreen fxPadScreen = FXPAD_SETUP;
bool fxPadArmed[2][3] = {{false, false, false}, {false, false, false}};
int fxPadActiveDeck = 0;  // 0 = Deck 1, 1 = Deck 2 — set by DECK1 / DECK2
bool fxPadTouching = false;
int fxPadLastX = -1;
int fxPadLastY = -1;
int fxPadCursorPx = -1;
int fxPadCursorPy = -1;
unsigned long fxPadGlowLastMs = 0;
uint8_t fxPadGlowPhase = 0;

// Fading glow trail left behind the finger
int16_t fxPadTrailX[FXPAD_TRAIL_LEN];
int16_t fxPadTrailY[FXPAD_TRAIL_LEN];
uint8_t fxPadTrailLife[FXPAD_TRAIL_LEN];
uint8_t fxPadTrailHead = 0;

int fxPadLearnLevel[3] = {0, 0, 0};  // values for the 3 Level sliders on MAP
int fxPadLearnY = FXPAD_COLOR_CENTER;
int fxPadLearnDragSlot = -1;  // 0-2 Level slider, or -1
bool fxPadLearnDraggingY = false;
bool fxPadLearnGateHeld = false;
int fxPadLearnDeck = 0;  // D1/D2 on MAP: which deck's 3 Level + CFX Y

void initializeFxPadMode();
void drawFxPadMode();
void handleFxPadMode();
void drawFxPadSetup();
void drawFxPadPad();
void drawFxPadMap();
void handleFxPadSetup();
void handleFxPadPad();
void handleFxPadMap();
void fxPadEngageEffects();
void fxPadReleaseEffects();
void fxPadSendArmedToggles(int deck);
void fxPadSendArmedLevels(int deck, byte value);
void fxPadDrawFxButtons(int deck);
void fxPadEraseCursor();
void fxPadDrawCursor(int px, int py);
void fxPadRestorePatch(int px, int py, int r);
void fxPadTrailClear();
void fxPadTrailAdd(int px, int py);
void fxPadTrailTick();
void fxPadDrawTrailBlob(int px, int py, uint8_t life);
void fxPadDrawPixelGlow(int px, int py, int radius, uint8_t brightness);
void fxPadDrawCrosshair();
void fxPadDrawLearnLevelSlider(int slot);
void fxPadDrawLearnYSlider();
void fxPadDrawLearnGate();
void fxPadRestorePadChrome();
void fxPadEnterPad(int deck);
byte fxPadLevelCc(int deck, int slot);
byte fxPadColorCc(int deck);
const byte *fxPadLevelCcs(int deck);

void initializeFxPadMode() {
  fxPadScreen = FXPAD_SETUP;
  fxPadActiveDeck = 0;
  fxPadTouching = false;
  fxPadLastX = -1;
  fxPadLastY = -1;
  fxPadCursorPx = -1;
  fxPadCursorPy = -1;
  fxPadGlowLastMs = 0;
  fxPadGlowPhase = 0;
  fxPadTrailClear();
  fxPadLearnDragSlot = -1;
  fxPadLearnDraggingY = false;
  fxPadLearnGateHeld = false;
  for (int i = 0; i < 3; i++) fxPadLearnLevel[i] = 0;
}

byte fxPadLevelCc(int deck, int slot) {
  return (deck == 0) ? fxPadLevelCcD1[slot] : fxPadLevelCcD2[slot];
}

const byte *fxPadLevelCcs(int deck) {
  return (deck == 0) ? fxPadLevelCcD1 : fxPadLevelCcD2;
}

byte fxPadColorCc(int deck) {
  return (deck == 0) ? CC_FXPAD_COLOR_Y_D1 : CC_FXPAD_COLOR_Y_D2;
}

// Reason: Effects paddle sends a Note On per slot; Rekordbox toggles On/Off
// on each. Gaps keep BLE notifies from coalescing; trailing gap is required
// so a following Level/CFX CC cannot overwrite the last FX On note.
void fxPadSendArmedToggles(int deck) {
  const byte *notes = (deck == 0) ? fxPadD1Notes : fxPadD2Notes;
  bool sent = false;
  for (int i = 0; i < 3; i++) {
    if (!fxPadArmed[deck][i]) continue;
    if (sent) delay(FXPAD_MIDI_GAP_MS);
    sendToggleNote(notes[i]);
    sent = true;
  }
  if (sent) delay(FXPAD_MIDI_GAP_MS);
}

// Send the same LevelDepth value to every armed FX slot on this deck.
void fxPadSendArmedLevels(int deck, byte value) {
  const byte *ccs = fxPadLevelCcs(deck);
  bool sent = false;
  for (int i = 0; i < 3; i++) {
    if (!fxPadArmed[deck][i]) continue;
    if (sent) delay(FXPAD_MIDI_GAP_MS);
    sendMIDI(0xB0, ccs[i], value);
    sent = true;
  }
  if (sent) delay(FXPAD_MIDI_GAP_MS);
}

void fxPadEngageEffects() {
  if (fxPadTouching) return;
  fxPadTouching = true;
  fxPadSendArmedToggles(fxPadActiveDeck);
}

void fxPadReleaseEffects() {
  if (!fxPadTouching && !fxPadLearnGateHeld) return;

  if (fxPadTouching) {
    // Order: FX Off toggles first (with trailing gap), then Level→0, CFX→center
    fxPadSendArmedToggles(fxPadActiveDeck);
    fxPadSendArmedLevels(fxPadActiveDeck, 0);
    sendMIDI(0xB0, fxPadColorCc(fxPadActiveDeck), FXPAD_COLOR_CENTER);
  } else if (fxPadLearnGateHeld) {
    fxPadSendArmedToggles(fxPadLearnDeck);
  }

  fxPadTouching = false;
  fxPadLearnGateHeld = false;
  fxPadLastX = -1;
  fxPadLastY = -1;
}

void fxPadEnterPad(int deck) {
  fxPadActiveDeck = deck;
  fxPadScreen = FXPAD_PAD;
  drawFxPadPad();
}

void drawFxPadMode() {
  if (fxPadScreen == FXPAD_PAD) {
    drawFxPadPad();
  } else if (fxPadScreen == FXPAD_MAP) {
    drawFxPadMap();
  } else {
    drawFxPadSetup();
  }
}

void fxPadDrawFxButtons(int deck) {
  int colX = (deck == 0) ? FXPAD_COL_L_X : FXPAD_COL_R_X;
  int btnStartX = colX + (FXPAD_COL_W - (FXPAD_FX_BTN_W * 3 + FXPAD_FX_BTN_GAP * 2)) / 2;
  for (int i = 0; i < 3; i++) {
    int bx = btnStartX + i * (FXPAD_FX_BTN_W + FXPAD_FX_BTN_GAP);
    String name = "FX" + String(i + 1);
    drawToggleButton(bx, FXPAD_FX_BTN_Y, FXPAD_FX_BTN_W, FXPAD_FX_BTN_H, name,
                     THEME_PRIMARY, fxPadArmed[deck][i]);
  }
}

void drawFxPadSetup() {
  tft.fillScreen(THEME_BG);
  drawHeader("PAD FX");

  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawCentreString("DECK 1", FXPAD_COL_L_X + FXPAD_COL_W / 2, CONTENT_Y + 2, 2);
  tft.drawCentreString("DECK 2", FXPAD_COL_R_X + FXPAD_COL_W / 2, CONTENT_Y + 2, 2);
  tft.drawFastVLine(160, CONTENT_Y, FXPAD_FX_BTN_Y + FXPAD_FX_BTN_H - CONTENT_Y,
                    THEME_SURFACE);

  fxPadDrawFxButtons(0);
  fxPadDrawFxButtons(1);

  tft.setTextColor(THEME_SECONDARY, THEME_BG);
  tft.drawCentreString("COLOR FX", 160, FXPAD_CFX_Y - 14, 1);
  drawRoundButton(FXPAD_CFX_PREV_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H,
                  "< CFX", THEME_SECONDARY, false);
  drawRoundButton(FXPAD_CFX_NEXT_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H,
                  "CFX >", THEME_SECONDARY, false);

  drawRoundButton(FXPAD_START1_X, FXPAD_START_Y, FXPAD_START_W, FXPAD_START_H,
                  "DECK1", THEME_PRIMARY, false);
  drawRoundButton(FXPAD_START2_X, FXPAD_START_Y, FXPAD_START_W, FXPAD_START_H,
                  "DECK2", THEME_ACCENT, false);
#if FXPAD_SHOW_MAP
  drawRoundButton(FXPAD_MAP_BTN_X, FXPAD_MAP_BTN_Y, FXPAD_MAP_W, FXPAD_MAP_H,
                  "MAP", THEME_WARNING, false);
#endif
}

void fxPadDrawLearnYSlider() {
  tft.fillRoundRect(FXPAD_LEARN_Y_X, FXPAD_LEARN_Y_Y,
                    FXPAD_LEARN_Y_W, FXPAD_LEARN_Y_H, 6, THEME_SURFACE);
  tft.drawRoundRect(FXPAD_LEARN_Y_X, FXPAD_LEARN_Y_Y,
                    FXPAD_LEARN_Y_W, FXPAD_LEARN_Y_H, 6, THEME_SECONDARY);
  int midY = FXPAD_LEARN_Y_Y + FXPAD_LEARN_Y_H / 2;
  tft.drawFastHLine(FXPAD_LEARN_Y_X, midY, FXPAD_LEARN_Y_W, THEME_TEXT_DIM);
  int knobY = map(fxPadLearnY, 127, 0,
                  FXPAD_LEARN_Y_Y + 4,
                  FXPAD_LEARN_Y_Y + FXPAD_LEARN_Y_H - 12);
  tft.fillRoundRect(FXPAD_LEARN_Y_X + 4, knobY,
                    FXPAD_LEARN_Y_W - 8, 8, 3, THEME_SECONDARY);
  tft.setTextColor(THEME_SECONDARY, THEME_BG);
  tft.drawCentreString("Y", FXPAD_LEARN_Y_X + FXPAD_LEARN_Y_W / 2,
                       FXPAD_LEARN_Y_Y - 14, 1);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("CFX", FXPAD_LEARN_Y_X + FXPAD_LEARN_Y_W / 2,
                       FXPAD_LEARN_Y_Y + FXPAD_LEARN_Y_H + 2, 1);
}

void fxPadDrawLearnLevelSlider(int slot) {
  int y = FXPAD_LEVEL_Y0 + slot * (FXPAD_LEVEL_H + FXPAD_LEVEL_GAP);
  uint16_t accent = (fxPadLearnDeck == 0) ? THEME_PRIMARY : THEME_ACCENT;
  tft.fillRoundRect(FXPAD_LEVEL_X, y, FXPAD_LEVEL_W, FXPAD_LEVEL_H, 6, THEME_SURFACE);
  tft.drawRoundRect(FXPAD_LEVEL_X, y, FXPAD_LEVEL_W, FXPAD_LEVEL_H, 6, accent);

  char label[12];
  snprintf(label, sizeof(label), "FX%d CC%d", slot + 1,
           fxPadLevelCc(fxPadLearnDeck, slot));
  tft.setTextColor(accent, THEME_SURFACE);
  tft.drawString(label, FXPAD_LEVEL_X + 6, y + 2, 1);

  int trackX = FXPAD_LEVEL_X + 6;
  int trackW = FXPAD_LEVEL_W - 12;
  int trackY = y + FXPAD_LEVEL_H - 10;
  tft.drawFastHLine(trackX, trackY + 3, trackW, THEME_TEXT_DIM);
  int knobX = map(fxPadLearnLevel[slot], 0, 127, trackX, trackX + trackW - 8);
  tft.fillRoundRect(knobX, trackY, 8, 8, 2, accent);
}

void fxPadDrawLearnGate() {
  drawRoundButton(FXPAD_LEARN_GATE_X, FXPAD_LEARN_GATE_Y,
                  FXPAD_LEARN_GATE_W, FXPAD_LEARN_GATE_H,
                  "GATE", THEME_ERROR, fxPadLearnGateHeld);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("hold = FX On", FXPAD_LEARN_GATE_X + FXPAD_LEARN_GATE_W + 8,
                 FXPAD_LEARN_GATE_Y + 10, 1);
}

void drawFxPadMap() {
  tft.fillScreen(THEME_BG);
  drawHeader("MIDI MAP");

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("DECK", 8, FXPAD_LEARN_DECK_Y + 6, 1);
  drawRoundButton(FXPAD_DECK1_X, FXPAD_LEARN_DECK_Y, FXPAD_DECK_W, FXPAD_DECK_H,
                  "D1", THEME_PRIMARY, fxPadLearnDeck == 0);
  drawRoundButton(FXPAD_DECK2_X, FXPAD_LEARN_DECK_Y, FXPAD_DECK_W, FXPAD_DECK_H,
                  "D2", THEME_ACCENT, fxPadLearnDeck == 1);

  fxPadDrawLearnYSlider();
  for (int i = 0; i < 3; i++) fxPadDrawLearnLevelSlider(i);
  fxPadDrawLearnGate();
}

void fxPadDrawCrosshair() {
  int cx = 160;
  int cy = 120;
  tft.drawFastHLine(12, cy, 296, FXPAD_CROSS_COLOR);
  tft.drawFastVLine(cx, 28, 200, FXPAD_CROSS_COLOR);
  tft.fillCircle(cx, cy, 2, FXPAD_CROSS_COLOR);
}

void fxPadRestorePadChrome() {
  uint16_t deckColor = (fxPadActiveDeck == 0) ? THEME_PRIMARY : THEME_ACCENT;
  tft.setTextColor(deckColor, FXPAD_PAD_BG);
  tft.drawString(fxPadActiveDeck == 0 ? "D1" : "D2", 292, 6, 2);

  tft.setTextColor(THEME_SECONDARY, FXPAD_PAD_BG);
  tft.drawCentreString("CFX", 160, 6, 2);
  tft.setTextColor(THEME_TEXT_DIM, FXPAD_PAD_BG);
  tft.drawCentreString("COLOR FX", 160, 224, 1);
  tft.setTextColor(THEME_PRIMARY, FXPAD_PAD_BG);
  tft.drawString("BEAT FX", 8, 112, 1);
  tft.setTextColor(THEME_TEXT_DIM, FXPAD_PAD_BG);
  tft.drawString("LEVEL", 268, 112, 1);

  // Armed FX summary for the active deck only
  char arms[20];
  int pos = 0;
  for (int i = 0; i < 3 && pos < 16; i++) {
    if (!fxPadArmed[fxPadActiveDeck][i]) continue;
    if (pos > 0) arms[pos++] = ' ';
    arms[pos++] = 'F';
    arms[pos++] = 'X';
    arms[pos++] = '1' + i;
  }
  arms[pos] = '\0';
  if (pos == 0) {
    tft.setTextColor(THEME_WARNING, FXPAD_PAD_BG);
    tft.drawCentreString("NO FX ARMED", 160, 28, 1);
  } else {
    tft.setTextColor(THEME_ACCENT, FXPAD_PAD_BG);
    tft.drawCentreString(arms, 160, 28, 1);
  }
  drawBackChevron();
}

void drawFxPadPad() {
  tft.fillScreen(FXPAD_PAD_BG);
  fxPadDrawCrosshair();
  fxPadRestorePadChrome();

  fxPadCursorPx = -1;
  fxPadCursorPy = -1;
  fxPadTouching = false;
  fxPadLastX = -1;
  fxPadLastY = -1;
  fxPadGlowPhase = 0;
  fxPadGlowLastMs = 0;
  fxPadTrailClear();
}

// Restore pad background + crosshair under a circular patch.
void fxPadRestorePatch(int px, int py, int r) {
  int x0 = constrain(px - r, 0, 319);
  int y0 = constrain(py - r, 0, 239);
  int x1 = constrain(px + r, 0, 319);
  int y1 = constrain(py + r, 0, 239);
  tft.fillRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, FXPAD_PAD_BG);

  int cx = 160;
  int cy = 120;
  if (y0 <= cy && y1 >= cy) {
    tft.drawFastHLine(x0, cy, x1 - x0 + 1, FXPAD_CROSS_COLOR);
  }
  if (x0 <= cx && x1 >= cx) {
    tft.drawFastVLine(cx, y0, y1 - y0 + 1, FXPAD_CROSS_COLOR);
  }
  if (abs(px - cx) <= r && abs(py - cy) <= r) {
    tft.fillCircle(cx, cy, 2, FXPAD_CROSS_COLOR);
  }
}

void fxPadTrailClear() {
  for (int i = 0; i < FXPAD_TRAIL_LEN; i++) {
    fxPadTrailLife[i] = 0;
  }
  fxPadTrailHead = 0;
}

// djay-style pixelated blue glow: blocky radial falloff, no outline/contour.
// brightness 0-255 scales overall intensity (for trail fade).
void fxPadDrawPixelGlow(int px, int py, int radius, uint8_t brightness) {
  if (radius < FXPAD_PIXEL_SIZE) radius = FXPAD_PIXEL_SIZE;
  int r2 = radius * radius;
  int step = FXPAD_PIXEL_SIZE;

  // Quantized neon blues — fewer steps = more posterized / pixelated look
  static const uint16_t palette[] = {
    0x0010,  // near-black blue (outer)
    0x00B5,
    0x019F,
    0x02BF,
    0x045F,
    0x05FF,
    0x07FF,  // cyan
    0xAFFF   // bright core
  };
  const int palN = 8;

  for (int by = -radius; by <= radius; by += step) {
    for (int bx = -radius; bx <= radius; bx += step) {
      int cx = bx + step / 2;
      int cy = by + step / 2;
      int d2 = cx * cx + cy * cy;
      if (d2 > r2) continue;

      // 0 at edge → 1 at center
      int fall = 255 - (d2 * 255) / r2;
      fall = (fall * brightness) / 255;
      // Posterize into palette bands
      int band = (fall * (palN - 1)) / 255;
      if (band < 1 && fall > 8) band = 1;  // keep a faint outer pixel ring
      if (band < 1) continue;              // skip near-bg cells (soft edge, no contour)

      int sx = px + bx;
      int sy = py + by;
      if (sx < 0 || sy < 0 || sx >= 320 || sy >= 240) continue;
      int w = step;
      int h = step;
      if (sx + w > 320) w = 320 - sx;
      if (sy + h > 240) h = 240 - sy;
      tft.fillRect(sx, sy, w, h, palette[band]);
    }
  }
}

void fxPadDrawTrailBlob(int px, int py, uint8_t life) {
  int radius = 6 + life;
  uint8_t brightness = (uint8_t)(40 + life * 20);
  fxPadDrawPixelGlow(px, py, radius, brightness);
}

void fxPadTrailAdd(int px, int py) {
  if (px < 0 || py < 0) return;
  fxPadTrailX[fxPadTrailHead] = (int16_t)px;
  fxPadTrailY[fxPadTrailHead] = (int16_t)py;
  fxPadTrailLife[fxPadTrailHead] = FXPAD_TRAIL_LIFE;
  fxPadTrailHead = (fxPadTrailHead + 1) % FXPAD_TRAIL_LEN;
  fxPadDrawTrailBlob(px, py, FXPAD_TRAIL_LIFE);
}

void fxPadTrailTick() {
  for (int i = 0; i < FXPAD_TRAIL_LEN; i++) {
    if (fxPadTrailLife[i] == 0) continue;
    int px = fxPadTrailX[i];
    int py = fxPadTrailY[i];
    int r = 6 + FXPAD_TRAIL_LIFE + FXPAD_PIXEL_SIZE;
    fxPadRestorePatch(px, py, r);
    fxPadTrailLife[i]--;
    if (fxPadTrailLife[i] > 0) {
      fxPadDrawTrailBlob(px, py, fxPadTrailLife[i]);
    }
  }
}

void fxPadEraseCursor() {
  if (fxPadCursorPx < 0) return;
  fxPadRestorePatch(fxPadCursorPx, fxPadCursorPy, FXPAD_GLOW_R_MAX + FXPAD_PIXEL_SIZE);
}

// Pixelated neon touch point (like djay Pro pad), no circle contour.
void fxPadDrawCursor(int px, int py) {
  fxPadEraseCursor();

  int pulse = fxPadGlowPhase;
  if (pulse > 8) pulse = 16 - pulse;
  // Snap radius to pixel grid so the blob stays blocky while pulsing
  int outer = 16 + (pulse / 2) * FXPAD_PIXEL_SIZE / 2;
  outer = (outer / FXPAD_PIXEL_SIZE) * FXPAD_PIXEL_SIZE;
  if (outer < 12) outer = 12;

  fxPadDrawPixelGlow(px, py, outer, 255);

  fxPadCursorPx = px;
  fxPadCursorPy = py;
}

void handleFxPadSetup() {
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  for (int deck = 0; deck < 2; deck++) {
    int colX = (deck == 0) ? FXPAD_COL_L_X : FXPAD_COL_R_X;
    int btnStartX = colX + (FXPAD_COL_W - (FXPAD_FX_BTN_W * 3 + FXPAD_FX_BTN_GAP * 2)) / 2;
    for (int i = 0; i < 3; i++) {
      int bx = btnStartX + i * (FXPAD_FX_BTN_W + FXPAD_FX_BTN_GAP);
      if (isButtonPressed(bx, FXPAD_FX_BTN_Y, FXPAD_FX_BTN_W, FXPAD_FX_BTN_H)) {
        // Reason: arm only — MIDI fires on pad touch (like Effects paddle)
        fxPadArmed[deck][i] = !fxPadArmed[deck][i];
        fxPadDrawFxButtons(deck);
        return;
      }
    }
  }

  if (isButtonPressed(FXPAD_CFX_PREV_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H)) {
    sendToggleNote(NOTE_CFX_SELECT_BACK);
    drawRoundButton(FXPAD_CFX_PREV_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H,
                    "< CFX", THEME_SECONDARY, true);
    delay(80);
    drawRoundButton(FXPAD_CFX_PREV_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H,
                    "< CFX", THEME_SECONDARY, false);
    return;
  }
  if (isButtonPressed(FXPAD_CFX_NEXT_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H)) {
    sendToggleNote(NOTE_CFX_SELECT_NEXT);
    drawRoundButton(FXPAD_CFX_NEXT_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H,
                    "CFX >", THEME_SECONDARY, true);
    delay(80);
    drawRoundButton(FXPAD_CFX_NEXT_X, FXPAD_CFX_Y, FXPAD_CFX_BTN_W, FXPAD_CFX_BTN_H,
                    "CFX >", THEME_SECONDARY, false);
    return;
  }

#if FXPAD_SHOW_MAP
  if (isButtonPressed(FXPAD_MAP_BTN_X, FXPAD_MAP_BTN_Y, FXPAD_MAP_W, FXPAD_MAP_H)) {
    fxPadScreen = FXPAD_MAP;
    fxPadLearnDragSlot = -1;
    fxPadLearnDraggingY = false;
    fxPadLearnGateHeld = false;
    drawFxPadMap();
    return;
  }
#endif

  if (isButtonPressed(FXPAD_START1_X, FXPAD_START_Y, FXPAD_START_W, FXPAD_START_H)) {
    fxPadEnterPad(0);
    return;
  }
  if (isButtonPressed(FXPAD_START2_X, FXPAD_START_Y, FXPAD_START_W, FXPAD_START_H)) {
    fxPadEnterPad(1);
    return;
  }
}

void handleFxPadMap() {
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    if (fxPadLearnGateHeld) {
      fxPadSendArmedToggles(fxPadLearnDeck);
      fxPadLearnGateHeld = false;
    }
    fxPadLearnDragSlot = -1;
    fxPadLearnDraggingY = false;
    fxPadScreen = FXPAD_SETUP;
    drawFxPadSetup();
    return;
  }

  if (touch.justPressed) {
    if (isButtonPressed(FXPAD_DECK1_X, FXPAD_LEARN_DECK_Y, FXPAD_DECK_W, FXPAD_DECK_H)) {
      fxPadLearnDeck = 0;
      fxPadLearnDragSlot = -1;
      drawFxPadMap();
      return;
    }
    if (isButtonPressed(FXPAD_DECK2_X, FXPAD_LEARN_DECK_Y, FXPAD_DECK_W, FXPAD_DECK_H)) {
      fxPadLearnDeck = 1;
      fxPadLearnDragSlot = -1;
      drawFxPadMap();
      return;
    }
    if (isButtonPressed(FXPAD_LEARN_GATE_X, FXPAD_LEARN_GATE_Y,
                        FXPAD_LEARN_GATE_W, FXPAD_LEARN_GATE_H)) {
      fxPadLearnGateHeld = true;
      fxPadSendArmedToggles(fxPadLearnDeck);
      fxPadDrawLearnGate();
    }
  }

  if (fxPadLearnGateHeld && touch.justReleased) {
    fxPadSendArmedToggles(fxPadLearnDeck);
    fxPadLearnGateHeld = false;
    fxPadDrawLearnGate();
  }

  // Vertical Y = CFX Parameter for selected deck only
  bool inY = touch.isPressed &&
             touch.x >= FXPAD_LEARN_Y_X &&
             touch.x <= FXPAD_LEARN_Y_X + FXPAD_LEARN_Y_W &&
             touch.y >= FXPAD_LEARN_Y_Y &&
             touch.y <= FXPAD_LEARN_Y_Y + FXPAD_LEARN_Y_H;

  if (inY && fxPadLearnDragSlot < 0) {
    int value = map(touch.y, FXPAD_LEARN_Y_Y, FXPAD_LEARN_Y_Y + FXPAD_LEARN_Y_H,
                    127, 0);
    value = constrain(value, 0, 127);
    fxPadLearnDraggingY = true;
    if (abs(value - fxPadLearnY) >= FXPAD_MIN_CHANGE) {
      fxPadLearnY = value;
      sendMIDI(0xB0, fxPadColorCc(fxPadLearnDeck), value);
      fxPadDrawLearnYSlider();
    }
  } else if (fxPadLearnDraggingY && !touch.isPressed) {
    fxPadLearnDraggingY = false;
  }

  // Three horizontal LevelDepth sliders for the selected deck
  if (!fxPadLearnDraggingY) {
    for (int slot = 0; slot < 3; slot++) {
      int y = FXPAD_LEVEL_Y0 + slot * (FXPAD_LEVEL_H + FXPAD_LEVEL_GAP);
      bool inSlot = touch.isPressed &&
                    touch.x >= FXPAD_LEVEL_X &&
                    touch.x <= FXPAD_LEVEL_X + FXPAD_LEVEL_W &&
                    touch.y >= y &&
                    touch.y <= y + FXPAD_LEVEL_H;
      if (inSlot && (fxPadLearnDragSlot < 0 || fxPadLearnDragSlot == slot)) {
        int value = map(touch.x, FXPAD_LEVEL_X, FXPAD_LEVEL_X + FXPAD_LEVEL_W, 0, 127);
        value = constrain(value, 0, 127);
        fxPadLearnDragSlot = slot;
        if (abs(value - fxPadLearnLevel[slot]) >= FXPAD_MIN_CHANGE) {
          fxPadLearnLevel[slot] = value;
          sendMIDI(0xB0, fxPadLevelCc(fxPadLearnDeck, slot), value);
          fxPadDrawLearnLevelSlider(slot);
        }
        break;
      }
    }
  }
  if (fxPadLearnDragSlot >= 0 && !touch.isPressed) {
    fxPadLearnDragSlot = -1;
  }
}

void handleFxPadPad() {
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    fxPadReleaseEffects();
    fxPadEraseCursor();
    fxPadTrailClear();
    fxPadScreen = FXPAD_SETUP;
    drawFxPadSetup();
    return;
  }

  bool onBack = isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H);

  if (touch.isPressed && !onBack) {
    int px = constrain(touch.x, 0, 319);
    int py = constrain(touch.y, 0, 239);

    int beatVal = constrain(map(px, 0, 319, 0, 127), 0, 127);
    int colorVal = constrain(map(py, 0, 239, 127, 0), 0, 127);

    unsigned long now = millis();
    bool glowTick = (now - fxPadGlowLastMs) >= FXPAD_GLOW_FRAME_MS;
    if (glowTick) {
      fxPadGlowLastMs = now;
      fxPadGlowPhase = (fxPadGlowPhase + 1) % 16;
      fxPadTrailTick();
    }

    if (!fxPadTouching) {
      fxPadEngageEffects();
      fxPadSendArmedLevels(fxPadActiveDeck, beatVal);
      sendMIDI(0xB0, fxPadColorCc(fxPadActiveDeck), colorVal);
      fxPadLastX = beatVal;
      fxPadLastY = colorVal;
      fxPadGlowPhase = 0;
      fxPadTrailClear();
      fxPadDrawCursor(px, py);
    } else {
      bool xChanged = abs(beatVal - fxPadLastX) >= FXPAD_MIN_CHANGE;
      bool yChanged = abs(colorVal - fxPadLastY) >= FXPAD_MIN_CHANGE;
      if (xChanged) {
        fxPadLastX = beatVal;
        fxPadSendArmedLevels(fxPadActiveDeck, beatVal);
      }
      if (yChanged) {
        fxPadLastY = colorVal;
        sendMIDI(0xB0, fxPadColorCc(fxPadActiveDeck), colorVal);
      }

      bool moved = abs(px - fxPadCursorPx) >= FXPAD_TRAIL_MIN_DIST ||
                   abs(py - fxPadCursorPy) >= FXPAD_TRAIL_MIN_DIST;
      // Drop a glow stamp at the previous point so motion leaves a fading trail
      if (moved && fxPadCursorPx >= 0) {
        fxPadTrailAdd(fxPadCursorPx, fxPadCursorPy);
      }
      if (xChanged || yChanged || glowTick || moved ||
          abs(px - fxPadCursorPx) >= 2 || abs(py - fxPadCursorPy) >= 2) {
        fxPadDrawCursor(px, py);
      }
    }
  } else if (fxPadTouching && (touch.justReleased || !touch.isPressed)) {
    fxPadReleaseEffects();
    fxPadEraseCursor();
    fxPadTrailClear();
    // Full redraw clears any remaining trail stamps cleanly
    tft.fillScreen(FXPAD_PAD_BG);
    fxPadDrawCrosshair();
    fxPadRestorePadChrome();
  }
}

void handleFxPadMode() {
  if (fxPadScreen == FXPAD_PAD) {
    handleFxPadPad();
  } else if (fxPadScreen == FXPAD_MAP) {
    handleFxPadMap();
  } else {
    handleFxPadSetup();
  }
}

#endif
