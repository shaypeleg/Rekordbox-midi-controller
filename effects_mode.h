#ifndef EFFECTS_MODE_H
#define EFFECTS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Layout constants
#define FX_COL_L_X 5
#define FX_COL_R_X 165
#define FX_COL_W 150
#define FX_BTN_W 44
#define FX_BTN_H 28
#define FX_BTN_GAP 6
#define FX_BTN_Y (CONTENT_Y + 24)
#define FX_PADDLE_Y (FX_BTN_Y + FX_BTN_H + 14)
#define FX_PADDLE_W 100
#define FX_PADDLE_H 120
#define FX_PADDLE_KNOB_H 40

// Per-deck state
bool fxD1Active[3] = {false, false, false};
bool fxD2Active[3] = {false, false, false};
bool fxD1PaddleDown = false;
bool fxD2PaddleDown = false;

// MIDI notes per deck (indexed 0-2 for FX1-FX3)
const byte fxD1Notes[3] = {NOTE_FX_D1_1, NOTE_FX_D1_2, NOTE_FX_D1_3};
const byte fxD2Notes[3] = {NOTE_FX_D2_1, NOTE_FX_D2_2, NOTE_FX_D2_3};

void initializeEffectsMode();
void drawEffectsMode();
void handleEffectsMode();
void drawFxDeckColumn(int colX, int deck);
void drawFxPaddle(int x, bool down);

void initializeEffectsMode() {
  for (int i = 0; i < 3; i++) {
    fxD1Active[i] = false;
    fxD2Active[i] = false;
  }
  fxD1PaddleDown = false;
  fxD2PaddleDown = false;
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
  bool *active = (deck == 1) ? fxD1Active : fxD2Active;
  bool paddleDown = (deck == 1) ? fxD1PaddleDown : fxD2PaddleDown;

  // Deck label
  tft.setTextColor(THEME_TEXT, THEME_BG);
  String label = "DECK " + String(deck);
  tft.drawCentreString(label, colX + FX_COL_W / 2, CONTENT_Y + 2, 2);

  // FX1, FX2, FX3 buttons in a row
  int btnStartX = colX + (FX_COL_W - (FX_BTN_W * 3 + FX_BTN_GAP * 2)) / 2;
  for (int i = 0; i < 3; i++) {
    int bx = btnStartX + i * (FX_BTN_W + FX_BTN_GAP);
    String name = "FX" + String(i + 1);
    drawToggleButton(bx, FX_BTN_Y, FX_BTN_W, FX_BTN_H, name,
                     THEME_PRIMARY, active[i]);
  }

  // Paddle switch (independent of FX buttons)
  int paddleX = colX + (FX_COL_W - FX_PADDLE_W) / 2;
  drawFxPaddle(paddleX, paddleDown);
}

void drawFxPaddle(int x, bool down) {
  uint16_t borderColor = THEME_PRIMARY;

  tft.fillRoundRect(x, FX_PADDLE_Y, FX_PADDLE_W, FX_PADDLE_H, 10,
                    THEME_SURFACE);
  tft.drawRoundRect(x, FX_PADDLE_Y, FX_PADDLE_W, FX_PADDLE_H, 10,
                    borderColor);

  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawCentreString("ON", x + FX_PADDLE_W / 2, FX_PADDLE_Y + 8, 1);
  tft.drawCentreString("OFF", x + FX_PADDLE_W / 2,
                       FX_PADDLE_Y + FX_PADDLE_H - 18, 1);

  int knobY = down
    ? (FX_PADDLE_Y + FX_PADDLE_H - FX_PADDLE_KNOB_H - 4)
    : (FX_PADDLE_Y + 4);

  uint16_t knobColor = down ? THEME_ACCENT : THEME_TEXT_DIM;

  tft.fillRoundRect(x + 4, knobY, FX_PADDLE_W - 8, FX_PADDLE_KNOB_H, 8,
                    knobColor);

  tft.setTextColor(THEME_BG, knobColor);
  tft.drawCentreString(down ? "ACTIVE" : "PUSH", x + FX_PADDLE_W / 2,
                       knobY + FX_PADDLE_KNOB_H / 2 - 6, 2);
}

void handleEffectsMode() {
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  for (int deck = 0; deck < 2; deck++) {
    int colX = (deck == 0) ? FX_COL_L_X : FX_COL_R_X;
    bool *active = (deck == 0) ? fxD1Active : fxD2Active;
    const byte *notes = (deck == 0) ? fxD1Notes : fxD2Notes;

    // FX buttons
    int btnStartX = colX + (FX_COL_W - (FX_BTN_W * 3 + FX_BTN_GAP * 2)) / 2;
    for (int i = 0; i < 3; i++) {
      int bx = btnStartX + i * (FX_BTN_W + FX_BTN_GAP);
      if (isButtonPressed(bx, FX_BTN_Y, FX_BTN_W, FX_BTN_H)) {
        active[i] = !active[i];
        sendToggleNote(notes[i]);
        drawFxDeckColumn(colX, deck + 1);
        return;
      }
    }

    // Paddle - sends its own dedicated note, independent of FX buttons
    int paddleX = colX + (FX_COL_W - FX_PADDLE_W) / 2;
    if (isButtonPressed(paddleX, FX_PADDLE_Y, FX_PADDLE_W, FX_PADDLE_H)) {
      bool *paddleDown = (deck == 0) ? &fxD1PaddleDown : &fxD2PaddleDown;
      byte paddleNote = (deck == 0) ? NOTE_FX_PADDLE_D1 : NOTE_FX_PADDLE_D2;

      *paddleDown = !(*paddleDown);
      sendToggleNote(paddleNote);
      drawFxDeckColumn(colX, deck + 1);
      return;
    }
  }
}

#endif
