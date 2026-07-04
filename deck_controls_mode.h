#ifndef DECK_CONTROLS_MODE_H
#define DECK_CONTROLS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

#define DC_COL_W 145
#define DC_ROW_H 38
#define DC_ROW_GAP 6
#define DC_COL1_X 10
#define DC_COL2_X 165
#define DC_ROW_Y0 (CONTENT_Y + 18)

bool deckTempo[2] = {false, false};
bool deckQuantize[2] = {false, false};
bool deckSlip[2] = {false, false};
bool deckVinyl[2] = {false, false};

// Function declarations
void initializeDeckControlsMode();
void drawDeckControlsMode();
void handleDeckControlsMode();

void initializeDeckControlsMode() {
  deckTempo[0] = deckTempo[1] = false;
  deckQuantize[0] = deckQuantize[1] = false;
  deckSlip[0] = deckSlip[1] = false;
  deckVinyl[0] = deckVinyl[1] = false;
}

void drawDeckControlsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("DECK CONTROLS");

  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString("DECK 1", DC_COL1_X + DC_COL_W / 2, CONTENT_Y, 2);
  tft.drawCentreString("DECK 2", DC_COL2_X + DC_COL_W / 2, CONTENT_Y, 2);

  for (int d = 0; d < 2; d++) {
    int x = (d == 0) ? DC_COL1_X : DC_COL2_X;
    drawToggleButton(x, DC_ROW_Y0, DC_COL_W, DC_ROW_H, "SLIP MODE", THEME_WARNING, deckSlip[d]);
    drawToggleButton(x, DC_ROW_Y0 + (DC_ROW_H + DC_ROW_GAP), DC_COL_W, DC_ROW_H, "QUANTIZE", THEME_ACCENT, deckQuantize[d]);
    drawToggleButton(x, DC_ROW_Y0 + 2 * (DC_ROW_H + DC_ROW_GAP), DC_COL_W, DC_ROW_H, "MASTER TEMPO", THEME_PRIMARY, deckTempo[d]);
    drawToggleButton(x, DC_ROW_Y0 + 3 * (DC_ROW_H + DC_ROW_GAP), DC_COL_W, DC_ROW_H, "VINYL", THEME_SECONDARY, deckVinyl[d]);
  }
}

void handleDeckControlsMode() {
  if (touch.justPressed && isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  for (int d = 0; d < 2; d++) {
    int x = (d == 0) ? DC_COL1_X : DC_COL2_X;
    int noteTempo = (d == 0) ? NOTE_D1_MASTER_TEMPO : NOTE_D2_MASTER_TEMPO;
    int noteQuantize = (d == 0) ? NOTE_D1_QUANTIZE : NOTE_D2_QUANTIZE;
    int noteSlip = (d == 0) ? NOTE_D1_SLIP : NOTE_D2_SLIP;
    int noteVinyl = (d == 0) ? NOTE_D1_VINYL : NOTE_D2_VINYL;

    if (isButtonPressed(x, DC_ROW_Y0, DC_COL_W, DC_ROW_H)) {
      deckSlip[d] = !deckSlip[d];
      sendToggleNote(noteSlip);
      Serial.printf("Deck %d Slip Mode: %s\n", d + 1, deckSlip[d] ? "ON" : "OFF");
      drawDeckControlsMode();
      return;
    }
    if (isButtonPressed(x, DC_ROW_Y0 + (DC_ROW_H + DC_ROW_GAP), DC_COL_W, DC_ROW_H)) {
      deckQuantize[d] = !deckQuantize[d];
      sendToggleNote(noteQuantize);
      Serial.printf("Deck %d Quantize: %s\n", d + 1, deckQuantize[d] ? "ON" : "OFF");
      drawDeckControlsMode();
      return;
    }
    if (isButtonPressed(x, DC_ROW_Y0 + 2 * (DC_ROW_H + DC_ROW_GAP), DC_COL_W, DC_ROW_H)) {
      deckTempo[d] = !deckTempo[d];
      sendToggleNote(noteTempo);
      Serial.printf("Deck %d Master Tempo: %s\n", d + 1, deckTempo[d] ? "ON" : "OFF");
      drawDeckControlsMode();
      return;
    }
    if (isButtonPressed(x, DC_ROW_Y0 + 3 * (DC_ROW_H + DC_ROW_GAP), DC_COL_W, DC_ROW_H)) {
      deckVinyl[d] = !deckVinyl[d];
      sendToggleNote(noteVinyl);
      Serial.printf("Deck %d Vinyl: %s\n", d + 1, deckVinyl[d] ? "ON" : "OFF");
      drawDeckControlsMode();
      return;
    }
  }
}

#endif
