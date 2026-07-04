#ifndef NEEDLE_SEARCH_MODE_H
#define NEEDLE_SEARCH_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

#define NS_STRIP_X 20
#define NS_STRIP_W 280
#define NS_STRIP_H 60
#define NS_D1_Y (CONTENT_Y + 18)
#define NS_D2_Y (NS_D1_Y + NS_STRIP_H + 30)

int nsLastValue[2] = {-1, -1};
bool nsDragging[2] = {false, false};

// Function declarations
void initializeNeedleSearchMode();
void drawNeedleSearchMode();
void handleNeedleSearchMode();
void drawNeedleStrip(int deck);
void handleNeedleStrip(int deck, int stripY);

void initializeNeedleSearchMode() {
  nsLastValue[0] = nsLastValue[1] = -1;
  nsDragging[0] = nsDragging[1] = false;
}

void drawNeedleSearchMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("NEEDLE SEARCH");
  drawNeedleStrip(0);
  drawNeedleStrip(1);
}

void drawNeedleStrip(int deck) {
  int y = (deck == 0) ? NS_D1_Y : NS_D2_Y;
  uint16_t color = (deck == 0) ? THEME_PRIMARY : THEME_ACCENT;

  tft.setTextColor(color, THEME_BG);
  tft.drawString(deck == 0 ? "DECK 1" : "DECK 2", NS_STRIP_X, y - 16, 2);

  tft.fillRoundRect(NS_STRIP_X, y, NS_STRIP_W, NS_STRIP_H, 6, THEME_SURFACE);
  tft.drawRoundRect(NS_STRIP_X, y, NS_STRIP_W, NS_STRIP_H, 6, color);

  // Track midpoint reference line
  tft.drawFastVLine(NS_STRIP_X + NS_STRIP_W / 2, y, NS_STRIP_H, THEME_TEXT_DIM);

  if (nsLastValue[deck] >= 0) {
    int px = NS_STRIP_X + map(nsLastValue[deck], 0, 127, 0, NS_STRIP_W);
    tft.fillRect(px - 2, y, 4, NS_STRIP_H, color);
  }
}

void handleNeedleSearchMode() {
  if (touch.justPressed && isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  handleNeedleStrip(0, NS_D1_Y);
  handleNeedleStrip(1, NS_D2_Y);
}

void handleNeedleStrip(int deck, int stripY) {
  bool inStrip = touch.isPressed &&
                 touch.x >= NS_STRIP_X && touch.x <= NS_STRIP_X + NS_STRIP_W &&
                 touch.y >= stripY && touch.y <= stripY + NS_STRIP_H;

  if (inStrip) {
    int value = map(touch.x, NS_STRIP_X, NS_STRIP_X + NS_STRIP_W, 0, 127);
    value = constrain(value, 0, 127);

    nsDragging[deck] = true;

    if (value != nsLastValue[deck]) {
      nsLastValue[deck] = value;
      sendMIDI(0xB0, (deck == 0) ? CC_NEEDLE_D1 : CC_NEEDLE_D2, value);
      drawNeedleStrip(deck);
    }
  } else if (nsDragging[deck] && !touch.isPressed) {
    // Lifting the finger is a pure UI reset, not a position command: the
    // marker returns to the neutral center line so the strip never implies
    // a stale "still holding this spot" position, but no MIDI is sent for
    // it - only an actual touch-drag sends CC values.
    nsDragging[deck] = false;
    nsLastValue[deck] = -1;
    drawNeedleStrip(deck);
  }
}

#endif
