#ifndef EFFECTS_MODE_H
#define EFFECTS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Layout constants
#define FX_COL_L_X 5
#define FX_COL_R_X 165
#define FX_COL_W 150
#define FX_BTN_W 46
#define FX_BTN_H 36
#define FX_BTN_GAP 5
#define FX_BTN_Y (CONTENT_Y + 24)
#define FX_PADDLE_Y (FX_BTN_Y + FX_BTN_H + 12)
#define FX_PADDLE_W 86
#define FX_PADDLE_H 100
#define FX_PADDLE_KNOB_H 34

// Paddle border turns orange when active
#define FX_PADDLE_ACTIVE_COLOR 0xFDA0

// Flash interval for active FX buttons (milliseconds)
#define FX_FLASH_INTERVAL 300

// Three-state FX model:
//   DISARMED - not selected (hollow button)
//   ARMED    - selected but paddle OFF (solid blue, no MIDI)
//   ACTIVE   - armed + paddle ON (solid blue + flashing, MIDI was sent)
enum FxState { FX_DISARMED, FX_ARMED, FX_ACTIVE };

FxState fxD1State[3] = {FX_DISARMED, FX_DISARMED, FX_DISARMED};
FxState fxD2State[3] = {FX_DISARMED, FX_DISARMED, FX_DISARMED};
bool fxD1PaddleDown = false;
bool fxD2PaddleDown = false;

// Shared flash phase for all active FX buttons
bool fxFlashVisible = true;
unsigned long fxLastFlashMs = 0;

// MIDI notes per deck (indexed 0-2 for FX1-FX3)
const byte fxD1Notes[3] = {NOTE_FX_D1_1, NOTE_FX_D1_2, NOTE_FX_D1_3};
const byte fxD2Notes[3] = {NOTE_FX_D2_1, NOTE_FX_D2_2, NOTE_FX_D2_3};

void initializeEffectsMode();
void drawEffectsMode();
void handleEffectsMode();
void drawFxDeckColumn(int colX, int deck);
void drawFxPaddle(int x, bool down);
void drawSingleFxButton(int colX, int deck, int slot);

bool hasAnyActiveFx() {
  for (int i = 0; i < 3; i++) {
    if (fxD1State[i] == FX_ACTIVE || fxD2State[i] == FX_ACTIVE) return true;
  }
  return false;
}

void initializeEffectsMode() {
  for (int i = 0; i < 3; i++) {
    fxD1State[i] = FX_DISARMED;
    fxD2State[i] = FX_DISARMED;
  }
  fxD1PaddleDown = false;
  fxD2PaddleDown = false;
  fxFlashVisible = true;
  fxLastFlashMs = 0;
}

// Determine the visual fill state for a single FX button.
bool fxButtonFilled(FxState s) {
  switch (s) {
    case FX_DISARMED: return false;
    case FX_ARMED:    return true;
    case FX_ACTIVE:   return fxFlashVisible;
  }
  return false;
}

// Redraw one FX button without touching anything else on screen.
void drawSingleFxButton(int colX, int deck, int slot) {
  FxState *state = (deck == 1) ? fxD1State : fxD2State;
  int btnStartX = colX + (FX_COL_W - (FX_BTN_W * 3 + FX_BTN_GAP * 2)) / 2;
  int bx = btnStartX + slot * (FX_BTN_W + FX_BTN_GAP);
  String name = "FX" + String(slot + 1);
  drawToggleButton(bx, FX_BTN_Y, FX_BTN_W, FX_BTN_H, name,
                   THEME_PRIMARY, fxButtonFilled(state[slot]));
}

void drawEffectsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("EFFECTS");

  int divX = 160;
  tft.drawFastVLine(divX, CONTENT_Y, 240 - CONTENT_Y, THEME_SURFACE);

  drawFxDeckColumn(FX_COL_L_X, 1);
  drawFxDeckColumn(FX_COL_R_X, 2);
}

void drawFxDeckColumn(int colX, int deck) {
  FxState *state = (deck == 1) ? fxD1State : fxD2State;
  bool paddleDown = (deck == 1) ? fxD1PaddleDown : fxD2PaddleDown;

  tft.setTextColor(THEME_TEXT, THEME_BG);
  String label = "DECK " + String(deck);
  tft.drawCentreString(label, colX + FX_COL_W / 2, CONTENT_Y + 2, 2);

  int btnStartX = colX + (FX_COL_W - (FX_BTN_W * 3 + FX_BTN_GAP * 2)) / 2;
  for (int i = 0; i < 3; i++) {
    int bx = btnStartX + i * (FX_BTN_W + FX_BTN_GAP);
    String name = "FX" + String(i + 1);
    drawToggleButton(bx, FX_BTN_Y, FX_BTN_W, FX_BTN_H, name,
                     THEME_PRIMARY, fxButtonFilled(state[i]));
  }

  int paddleX = colX + (FX_COL_W - FX_PADDLE_W) / 2;
  drawFxPaddle(paddleX, paddleDown);
}

void drawFxPaddle(int x, bool down) {
  uint16_t borderColor = down ? FX_PADDLE_ACTIVE_COLOR : THEME_PRIMARY;

  tft.fillRoundRect(x, FX_PADDLE_Y, FX_PADDLE_W, FX_PADDLE_H, 10,
                    down ? FX_PADDLE_ACTIVE_COLOR : THEME_SURFACE);
  tft.drawRoundRect(x, FX_PADDLE_Y, FX_PADDLE_W, FX_PADDLE_H, 10,
                    borderColor);

  uint16_t bgFill = down ? FX_PADDLE_ACTIVE_COLOR : THEME_SURFACE;
  tft.setTextColor(down ? THEME_BG : THEME_TEXT_DIM, bgFill);
  tft.drawCentreString("ON", x + FX_PADDLE_W / 2, FX_PADDLE_Y + 8, 1);
  tft.drawCentreString("OFF", x + FX_PADDLE_W / 2,
                       FX_PADDLE_Y + FX_PADDLE_H - 18, 1);

  int knobY = down
    ? (FX_PADDLE_Y + 4)
    : (FX_PADDLE_Y + FX_PADDLE_H - FX_PADDLE_KNOB_H - 4);

  uint16_t knobColor = down ? THEME_ACCENT : THEME_TEXT_DIM;

  tft.fillRoundRect(x + 4, knobY, FX_PADDLE_W - 8, FX_PADDLE_KNOB_H, 8,
                    knobColor);

  tft.setTextColor(THEME_BG, knobColor);
  tft.drawCentreString(down ? "ACTIVE" : "PUSH", x + FX_PADDLE_W / 2,
                       knobY + FX_PADDLE_KNOB_H / 2 - 6, 2);
}

void handleEffectsMode() {
  // --- Flash animation (runs every frame, not just on touch) ---
  if (hasAnyActiveFx()) {
    unsigned long now = millis();
    if (now - fxLastFlashMs >= FX_FLASH_INTERVAL) {
      fxFlashVisible = !fxFlashVisible;
      fxLastFlashMs = now;
      for (int deck = 0; deck < 2; deck++) {
        FxState *st = (deck == 0) ? fxD1State : fxD2State;
        int colX = (deck == 0) ? FX_COL_L_X : FX_COL_R_X;
        for (int i = 0; i < 3; i++) {
          if (st[i] == FX_ACTIVE) drawSingleFxButton(colX, deck + 1, i);
        }
      }
    }
  }

  // --- Back button ---
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  // --- Per-deck touch handling ---
  for (int deck = 0; deck < 2; deck++) {
    int colX = (deck == 0) ? FX_COL_L_X : FX_COL_R_X;
    FxState *state = (deck == 0) ? fxD1State : fxD2State;
    const byte *notes = (deck == 0) ? fxD1Notes : fxD2Notes;
    bool *paddleDown = (deck == 0) ? &fxD1PaddleDown : &fxD2PaddleDown;

    // FX buttons
    int btnStartX = colX + (FX_COL_W - (FX_BTN_W * 3 + FX_BTN_GAP * 2)) / 2;
    for (int i = 0; i < 3; i++) {
      int bx = btnStartX + i * (FX_BTN_W + FX_BTN_GAP);
      if (isButtonPressed(bx, FX_BTN_Y, FX_BTN_W, FX_BTN_H)) {
        if (*paddleDown) {
          if (state[i] == FX_ACTIVE) {
            state[i] = FX_DISARMED;
            sendToggleNote(notes[i]);
            Serial.printf("Deck %d FX%d: active → disarmed (MIDI off)\n", deck + 1, i + 1);
          } else {
            state[i] = FX_ACTIVE;
            sendToggleNote(notes[i]);
            Serial.printf("Deck %d FX%d: → active (MIDI on)\n", deck + 1, i + 1);
          }
        } else {
          if (state[i] == FX_ARMED) {
            state[i] = FX_DISARMED;
            Serial.printf("Deck %d FX%d: armed → disarmed\n", deck + 1, i + 1);
          } else {
            state[i] = FX_ARMED;
            Serial.printf("Deck %d FX%d: → armed\n", deck + 1, i + 1);
          }
        }
        drawFxDeckColumn(colX, deck + 1);
        return;
      }
    }

    // Paddle switch (no MIDI from paddle itself)
    int paddleX = colX + (FX_COL_W - FX_PADDLE_W) / 2;
    if (isButtonPressed(paddleX, FX_PADDLE_Y, FX_PADDLE_W, FX_PADDLE_H)) {
      *paddleDown = !(*paddleDown);

      if (*paddleDown) {
        // Paddle ON: all armed FX → active, send MIDI for each.
        // BLE needs a gap between consecutive notify() calls so the
        // receiver sees each message as a separate packet.
        bool first = true;
        for (int i = 0; i < 3; i++) {
          if (state[i] == FX_ARMED) {
            if (!first) delay(20);
            first = false;
            state[i] = FX_ACTIVE;
            sendToggleNote(notes[i]);
            Serial.printf("Deck %d FX%d: armed → active (paddle ON)\n", deck + 1, i + 1);
          }
        }
        fxFlashVisible = true;
        fxLastFlashMs = millis();
      } else {
        // Paddle OFF: all active FX → armed, send MIDI for each
        bool first = true;
        for (int i = 0; i < 3; i++) {
          if (state[i] == FX_ACTIVE) {
            if (!first) delay(20);
            first = false;
            state[i] = FX_ARMED;
            sendToggleNote(notes[i]);
            Serial.printf("Deck %d FX%d: active → armed (paddle OFF)\n", deck + 1, i + 1);
          }
        }
      }

      drawFxDeckColumn(colX, deck + 1);
      return;
    }
  }
}

#endif
