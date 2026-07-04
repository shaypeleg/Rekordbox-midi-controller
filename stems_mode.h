#ifndef STEMS_MODE_H
#define STEMS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

#define ST_COL_W 70
#define ST_COL_GAP 6
#define ST_START_X 10
#define ST_ROW_H 50

// Per-deck label/SOLO row, directly above that deck's stem buttons.
#define ST_LABEL_ROW_H 28
#define ST_LABEL_GAP 3
#define ST_D1_LABEL_Y CONTENT_Y
#define ST_D1_Y (ST_D1_LABEL_Y + ST_LABEL_ROW_H + ST_LABEL_GAP)
#define ST_D2_LABEL_Y (ST_D1_Y + ST_ROW_H + 8)
#define ST_D2_Y (ST_D2_LABEL_Y + ST_LABEL_ROW_H + ST_LABEL_GAP)

// SOLO toggle slider, right side of each deck's label row. The hit area
// covers the "SOLO" label + the pill track so a loose finger tap anywhere
// in that zone registers.
#define ST_SOLO_W 100
#define ST_SOLO_H ST_LABEL_ROW_H
#define ST_SOLO_X (320 - 10 - ST_SOLO_W)

bool stemActive[2][4] = {{false, false, false, false}, {false, false, false, false}};
// Global solo mode — Rekordbox "ActiveStem Mute/Solo" is deck-independent.
bool stemSoloMode = false;
// Which stem is visually soloed per deck (-1 = none). Only affects the
// on-screen red/active display — MIDI is always just Note On for the tapped stem.
int soloedStem[2] = {-1, -1};
const char *stemLabels[4] = {"VOCAL", "MELODY", "BASS", "DRUMS"};

// Function declarations
void initializeStemsMode();
void drawStemsMode();
void handleStemsMode();
void drawStemRow(int deck);
void handleStemRow(int deck, int rowY);
void setStemState(int deck, int stem, bool desired);
void applyStemSolo(int deck, int stem);
int stemNoteFor(int deck, int stem);
int stemLabelY(int deck);
int stemRowY(int deck);
bool isStemMuted(int deck, int stem);
void drawStemButton(int x, int y, int w, int h, const char *label, uint16_t color, bool muted);

void initializeStemsMode() {
  stemSoloMode = false;
  for (int d = 0; d < 2; d++) {
    soloedStem[d] = -1;
    for (int s = 0; s < 4; s++)
      stemActive[d][s] = false;
  }
}

int stemNoteFor(int deck, int stem) {
  // Order matches stemLabels[]: Vocal, Melody, Bass, Drums.
  static const int d1Notes[4] = {NOTE_STEM_D1_VOCAL, NOTE_STEM_D1_MELODY, NOTE_STEM_D1_BASS, NOTE_STEM_D1_DRUMS};
  static const int d2Notes[4] = {NOTE_STEM_D2_VOCAL, NOTE_STEM_D2_MELODY, NOTE_STEM_D2_BASS, NOTE_STEM_D2_DRUMS};
  return (deck == 0) ? d1Notes[stem] : d2Notes[stem];
}

void drawStemsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("STEMS");
  drawStemRow(0);
  drawStemRow(1);
}

int stemLabelY(int deck) { return (deck == 0) ? ST_D1_LABEL_Y : ST_D2_LABEL_Y; }
int stemRowY(int deck)   { return (deck == 0) ? ST_D1_Y : ST_D2_Y; }

bool isStemMuted(int deck, int stem) {
  if (stemSoloMode) {
    if (soloedStem[deck] == -1) return false;
    return stem != soloedStem[deck];
  }
  return stemActive[deck][stem];
}

// Same pill button as everywhere else, except a muted stem always renders
// as a solid red block regardless of deck color - red-for-muted needs to
// stay instantly recognizable rather than blend in with the deck's accent.
void drawStemButton(int x, int y, int w, int h, const char *label, uint16_t color, bool muted) {
  drawRoundButton(x, y, w, h, label, muted ? THEME_ERROR : color, muted);
}

void drawStemRow(int deck) {
  int labelY = stemLabelY(deck);
  int y = stemRowY(deck);
  uint16_t color = (deck == 0) ? THEME_PRIMARY : THEME_ACCENT;

  tft.setTextColor(color, THEME_BG);
  tft.drawString(deck == 0 ? "DECK 1" : "DECK 2", ST_START_X, labelY + ST_LABEL_ROW_H / 2 - 8, 2);

  // Single global SOLO toggle — drawn on deck 1's row only.
  if (deck == 0) {
    drawToggleSlider(ST_SOLO_X, labelY, ST_SOLO_W, ST_SOLO_H, "SOLO", color, stemSoloMode);
  }

  for (int s = 0; s < 4; s++) {
    int x = ST_START_X + s * (ST_COL_W + ST_COL_GAP);
    drawStemButton(x, y, ST_COL_W, ST_ROW_H, stemLabels[s], color, isStemMuted(deck, s));
  }
}

void handleStemsMode() {
  if (touch.justPressed && isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  // Single global SOLO toggle (hit area is on deck 1's label row).
  if (isButtonPressed(ST_SOLO_X, stemLabelY(0), ST_SOLO_W, ST_SOLO_H)) {
    stemSoloMode = !stemSoloMode;

    if (stemSoloMode) {
      // Entering SOLO mode — check if stem state is "solo-compatible".
      // Valid states: 0 muted (clean) or 3 muted (already soloed).
      // Invalid: 1 or 2 muted — Rekordbox resets them when StemsMode is
      // toggled, so just update local UI to match (no stem MIDI needed).
      for (int d = 0; d < 2; d++) {
        int mutedCount = 0;
        for (int s = 0; s < 4; s++) {
          if (stemActive[d][s]) mutedCount++;
        }
        if (mutedCount == 1 || mutedCount == 2) {
          for (int s = 0; s < 4; s++) stemActive[d][s] = false;
          soloedStem[d] = -1;
        } else if (mutedCount == 3) {
          for (int s = 0; s < 4; s++) {
            if (!stemActive[d][s]) { soloedStem[d] = s; break; }
          }
        } else {
          soloedStem[d] = -1;
        }
      }
    }

    sendToggleNote(NOTE_STEMS_MODE_D1);
    Serial.printf("StemsMode toggled: %s\n", stemSoloMode ? "SOLO" : "NORMAL");
    drawStemRow(0);
    drawStemRow(1);
    return;
  }

  handleStemRow(0, ST_D1_Y);
  handleStemRow(1, ST_D2_Y);
}

// Toggles a single stem's Note On/Off to reach `desired`, mirroring the new
// state locally. No-op if the stem is already in the desired state, since
// Rekordbox flips its own on/off state on every Note On - sending one when
// nothing should change would desync our local mirror from reality.
void setStemState(int deck, int stem, bool desired) {
  if (stemActive[deck][stem] == desired) return;
  sendToggleNote(stemNoteFor(deck, stem));
  stemActive[deck][stem] = desired;
}

void applyStemSolo(int deck, int stem) {
  // MIDI: always just send Note On for the tapped stem. Rekordbox is in
  // StemsMode=solo so it handles the "mute others" logic internally.
  sendToggleNote(stemNoteFor(deck, stem));

  // Update both visual solo tracking AND stemActive so that switching
  // back to NORMAL mode preserves the correct muted/unmuted display.
  if (soloedStem[deck] == stem) {
    soloedStem[deck] = -1;
    for (int x = 0; x < 4; x++) stemActive[deck][x] = false;
  } else {
    soloedStem[deck] = stem;
    for (int x = 0; x < 4; x++) stemActive[deck][x] = (x != stem);
  }
}

void handleStemRow(int deck, int rowY) {
  for (int s = 0; s < 4; s++) {
    int x = ST_START_X + s * (ST_COL_W + ST_COL_GAP);
    if (isButtonPressed(x, rowY, ST_COL_W, ST_ROW_H)) {
      if (stemSoloMode) {
        applyStemSolo(deck, s);
      } else {
        setStemState(deck, s, !stemActive[deck][s]);
      }
      Serial.printf("Deck %d %s: %s\n", deck + 1, stemLabels[s], isStemMuted(deck, s) ? "MUTED" : "PLAYING");
      drawStemRow(deck);
      return;
    }
  }
}

#endif
