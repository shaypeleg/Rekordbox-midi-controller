#ifndef NEEDLE_SEARCH_MODE_H
#define NEEDLE_SEARCH_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

#define NS_STRIP_X 20
#define NS_STRIP_W 280
#define NS_STRIP_H 44
#define NS_D1_Y (CONTENT_Y + 18)
#define NS_D2_Y (NS_D1_Y + NS_STRIP_H + NS_CUE_BTN_H + 22)

// Cue point button layout
#define NS_CUE_BTN_W 60
#define NS_CUE_BTN_H 26
#define NS_CUE_BTN_GAP 8
#define NS_CUE_BTN_Y_OFFSET 4

// Minimum CC change before a new MIDI message is sent.  With MIN_CHANGE=3
// the finger must move ~6-7 pixels before Rekordbox sees a new position,
// preventing the rapid small jumps caused by touchscreen jitter.
#define NS_MIN_CHANGE 3

int nsLastValue[2] = {-1, -1};
bool nsDragging[2] = {false, false};

// Function declarations
void initializeNeedleSearchMode();
void drawNeedleSearchMode();
void handleNeedleSearchMode();
void drawNeedleStrip(int deck);
void drawCueButtons(int deck);
void handleNeedleStrip(int deck, int stripY);
void handleCueButtons(int deck);

void initializeNeedleSearchMode() {
  nsLastValue[0] = nsLastValue[1] = -1;
  nsDragging[0] = nsDragging[1] = false;
}

void drawNeedleSearchMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("SONG SEARCH");
  drawNeedleStrip(0);
  drawCueButtons(0);
  drawNeedleStrip(1);
  drawCueButtons(1);
}

void drawNeedleStrip(int deck) {
  int y = (deck == 0) ? NS_D1_Y : NS_D2_Y;
  uint16_t color = (deck == 0) ? THEME_PRIMARY : THEME_ACCENT;

  tft.setTextColor(color, THEME_BG);
  tft.drawString(deck == 0 ? "DECK 1" : "DECK 2", NS_STRIP_X, y - 16, 2);

  tft.fillRoundRect(NS_STRIP_X, y, NS_STRIP_W, NS_STRIP_H, 6, THEME_SURFACE);
  tft.drawRoundRect(NS_STRIP_X, y, NS_STRIP_W, NS_STRIP_H, 6, color);

  tft.drawFastVLine(NS_STRIP_X + NS_STRIP_W / 2, y, NS_STRIP_H, THEME_TEXT_DIM);

  if (nsLastValue[deck] >= 0) {
    int px = NS_STRIP_X + map(nsLastValue[deck], 0, 127, 0, NS_STRIP_W);
    tft.fillRect(px - 2, y, 4, NS_STRIP_H, color);
  }
}

void drawCueButtons(int deck) {
  int stripY = (deck == 0) ? NS_D1_Y : NS_D2_Y;
  int btnY = stripY + NS_STRIP_H + NS_CUE_BTN_Y_OFFSET;
  uint16_t color = (deck == 0) ? THEME_PRIMARY : THEME_ACCENT;

  int prevX = NS_STRIP_X + NS_STRIP_W - (NS_CUE_BTN_W * 2 + NS_CUE_BTN_GAP);
  int nextX = NS_STRIP_X + NS_STRIP_W - NS_CUE_BTN_W;

  drawRoundButton(prevX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H, "< CUE", color, false);
  drawRoundButton(nextX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H, "CUE >", color, false);
}

void handleNeedleSearchMode() {
  if (touch.justPressed && isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  handleNeedleStrip(0, NS_D1_Y);
  handleNeedleStrip(1, NS_D2_Y);
  handleCueButtons(0);
  handleCueButtons(1);
}

void handleCueButtons(int deck) {
  if (!touch.justPressed) return;

  int stripY = (deck == 0) ? NS_D1_Y : NS_D2_Y;
  int btnY = stripY + NS_STRIP_H + NS_CUE_BTN_Y_OFFSET;

  int prevX = NS_STRIP_X + NS_STRIP_W - (NS_CUE_BTN_W * 2 + NS_CUE_BTN_GAP);
  int nextX = NS_STRIP_X + NS_STRIP_W - NS_CUE_BTN_W;

  if (isButtonPressed(prevX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H)) {
    sendToggleNote((deck == 0) ? NOTE_CUE_PREV_D1 : NOTE_CUE_PREV_D2);
    uint16_t color = (deck == 0) ? THEME_PRIMARY : THEME_ACCENT;
    drawRoundButton(prevX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H, "< CUE", color, true);
    delay(100);
    drawRoundButton(prevX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H, "< CUE", color, false);
  } else if (isButtonPressed(nextX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H)) {
    sendToggleNote((deck == 0) ? NOTE_CUE_NEXT_D1 : NOTE_CUE_NEXT_D2);
    uint16_t color = (deck == 0) ? THEME_PRIMARY : THEME_ACCENT;
    drawRoundButton(nextX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H, "CUE >", color, true);
    delay(100);
    drawRoundButton(nextX, btnY, NS_CUE_BTN_W, NS_CUE_BTN_H, "CUE >", color, false);
  }
}

void handleNeedleStrip(int deck, int stripY) {
  bool inStrip = touch.isPressed &&
                 touch.x >= NS_STRIP_X && touch.x <= NS_STRIP_X + NS_STRIP_W &&
                 touch.y >= stripY && touch.y <= stripY + NS_STRIP_H;

  if (inStrip) {
    int value = map(touch.x, NS_STRIP_X, NS_STRIP_X + NS_STRIP_W, 0, 127);
    value = constrain(value, 0, 127);

    nsDragging[deck] = true;

    bool changed = (nsLastValue[deck] < 0) ||
                   (abs(value - nsLastValue[deck]) >= NS_MIN_CHANGE);
    if (changed) {
      nsLastValue[deck] = value;
      sendMIDI(0xB0, (deck == 0) ? CC_NEEDLE_D1 : CC_NEEDLE_D2, value);
      drawNeedleStrip(deck);
    }
  } else if (nsDragging[deck] && !touch.isPressed) {
    nsDragging[deck] = false;
    nsLastValue[deck] = -1;
    drawNeedleStrip(deck);
  }
}

#endif
