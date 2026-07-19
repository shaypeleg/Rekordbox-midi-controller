#ifndef DECK_CONTROLS_MODE_H
#define DECK_CONTROLS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

#define DC_COL_W 145
#define DC_ROW_H 30
#define DC_ROW_GAP 3
#define DC_COL1_X 10
#define DC_COL2_X 165
#define DC_ROW_Y0 (CONTENT_Y + 18)

// Auto Cue status line + toggle, below the 4 per-deck rows. Full width
// (spans both columns) since the behaviour isn't per-deck - it's one
// switch that watches the crossfader and drives whichever deck's Cue
// needs to turn on/off.
#define DC_AUTOCUE_STATUS_Y (DC_ROW_Y0 + 4 * (DC_ROW_H + DC_ROW_GAP) + 4)
#define DC_AUTOCUE_Y (DC_AUTOCUE_STATUS_Y + 10)
#define DC_AUTOCUE_H 24
#define DC_AUTOCUE_W (DC_COL2_X + DC_COL_W - DC_COL1_X)

// Auto Cue crossfader zones use hysteresis so tiny edge bounce (0↔4)
// doesn't spam Cue toggles. Entering an end is sticky until the fader
// moves clearly into the middle (or jumps to the other end).
#define XF_ENTER_LEFT   2
#define XF_LEAVE_LEFT  10
#define XF_ENTER_RIGHT 125
#define XF_LEAVE_RIGHT 117
// Fast left↔right swipes briefly pass through the middle; wait before
// treating that as "release Cue" so we don't toggle OFF then ON (and
// desync Rekordbox if a BLE Note On is dropped).
#define XF_NONE_DEBOUNCE_MS 50

bool deckTempo[2] = {false, false};
bool deckQuantize[2] = {false, false};
bool deckSlip[2] = {false, false};
bool deckVinyl[2] = {false, false};

// Auto Cue: when enabled, automatically enables headphone Cue on whichever
// deck the crossfader has just silenced, and turns it back off once the
// fader leaves that end. cueOnD1/D2 mirror what we believe Rekordbox's
// Cue state is *because of this feature* - not the user's own Cue taps
// elsewhere, which this device has no visibility into.
bool autoCueEnabled = false;
bool cueOnD1 = false;
bool cueOnD2 = false;
enum CrossfaderZone { XF_ZONE_NONE, XF_ZONE_LEFT, XF_ZONE_RIGHT };
CrossfaderZone xfZone = XF_ZONE_NONE;
CrossfaderZone xfPendingZone = XF_ZONE_NONE;
unsigned long xfPendingSince = 0;

// Function declarations
void initializeDeckControlsMode();
void drawDeckControlsMode();
void handleDeckControlsMode();
void drawAutoCueStatus();
void updateAutoCue();
void setCueState(int deck, bool desired);
void applyAutoCueZone(CrossfaderZone zone);
CrossfaderZone classifyXfZone(int value, CrossfaderZone current);

void initializeDeckControlsMode() {
  deckTempo[0] = deckTempo[1] = false;
  deckQuantize[0] = deckQuantize[1] = false;
  deckSlip[0] = deckSlip[1] = false;
  deckVinyl[0] = deckVinyl[1] = false;
  autoCueEnabled = false;
  cueOnD1 = cueOnD2 = false;
  xfZone = XF_ZONE_NONE;
  xfPendingZone = XF_ZONE_NONE;
  xfPendingSince = 0;
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

  drawAutoCueStatus();
  drawToggleSlider(DC_COL1_X, DC_AUTOCUE_Y, DC_AUTOCUE_W, DC_AUTOCUE_H, "AUTO CUE", THEME_SUCCESS, autoCueEnabled);
}

// Small debug/status line above the Auto Cue toggle - shows the live
// crossfader feedback value (so the CSV mapping can be verified) and
// which deck's Cue this feature currently believes it has turned on.
void drawAutoCueStatus() {
  tft.fillRect(DC_COL1_X, DC_AUTOCUE_STATUS_Y, DC_AUTOCUE_W, 10, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);

  int value = lastReceivedCC[CC_CROSSFADER_FEEDBACK];
  String status = "XFADE: " + (value < 0 ? String("no data") : String(value));
  status += "   D1 CUE:" + String(cueOnD1 ? "ON" : "-");
  status += "  D2 CUE:" + String(cueOnD2 ? "ON" : "-");
  tft.drawString(status, DC_COL1_X, DC_AUTOCUE_STATUS_Y, 1);
}

// Sends Cue On/Off only when the desired state actually changes, mirroring
// the same "no-op if already there" pattern as setStemState() in
// stems_mode.h - Rekordbox toggles its own state on every Note On, so
// resending when nothing should change would desync our local mirror.
// Uses the DDJ-REV5 Cue Learn codes: 9007 (Deck 1) / 9107 (Deck 2).
void setCueState(int deck, bool desired) {
  bool *cueOn = (deck == 0) ? &cueOnD1 : &cueOnD2;
  if (*cueOn == desired) return;
  byte status = (deck == 0) ? MIDI_CH_D1_CUE : MIDI_CH_D2_CUE;
  sendMIDI(status, NOTE_CUE, 127);
  *cueOn = desired;
}

CrossfaderZone classifyXfZone(int value, CrossfaderZone current) {
  if (current == XF_ZONE_LEFT) {
    if (value > XF_LEAVE_LEFT) {
      return (value >= XF_ENTER_RIGHT) ? XF_ZONE_RIGHT : XF_ZONE_NONE;
    }
    return XF_ZONE_LEFT;
  }
  if (current == XF_ZONE_RIGHT) {
    if (value < XF_LEAVE_RIGHT) {
      return (value <= XF_ENTER_LEFT) ? XF_ZONE_LEFT : XF_ZONE_NONE;
    }
    return XF_ZONE_RIGHT;
  }
  if (value <= XF_ENTER_LEFT) return XF_ZONE_LEFT;
  if (value >= XF_ENTER_RIGHT) return XF_ZONE_RIGHT;
  return XF_ZONE_NONE;
}

// Always turn the unwanted Cue off first, then the wanted one on - avoids a
// brief both-on window and prefers "off" winning if BLE drops a packet.
void applyAutoCueZone(CrossfaderZone zone) {
  if (zone == XF_ZONE_LEFT) {
    setCueState(0, false);
    setCueState(1, true);   // 9107
  } else if (zone == XF_ZONE_RIGHT) {
    setCueState(1, false);
    setCueState(0, true);   // 9007
  } else {
    setCueState(0, false);
    setCueState(1, false);
  }
}

// Polls the crossfader feedback CC and edge-triggers Cue on/off as the
// fader crosses into or out of either end zone. Runs every loop() tick
// regardless of which screen is showing, since Auto Cue is a background
// safety behaviour, not something tied to the Deck Controls screen being
// open. Only redraws if that screen happens to be visible right now.
void updateAutoCue() {
  if (!autoCueEnabled) return;

  int value = lastReceivedCC[CC_CROSSFADER_FEEDBACK];
  if (value < 0) return;  // no feedback received yet - nothing to act on

  CrossfaderZone raw = classifyXfZone(value, xfZone);

  if (raw == xfZone) {
    xfPendingZone = xfZone;
    return;
  }

  // Leaving an end briefly while slamming to the other side must NOT fire
  // Cue-off - that extra toggle is what desyncs Rekordbox on fast moves.
  if (raw == XF_ZONE_NONE) {
    if (xfPendingZone != XF_ZONE_NONE) {
      xfPendingZone = XF_ZONE_NONE;
      xfPendingSince = millis();
      return;
    }
    if (millis() - xfPendingSince < XF_NONE_DEBOUNCE_MS) return;
  }

  xfPendingZone = raw;
  xfZone = raw;
  applyAutoCueZone(raw);

  Serial.printf("Auto Cue: crossfader=%d zone=%d D1=%s D2=%s\n",
                value, raw, cueOnD1 ? "ON" : "OFF", cueOnD2 ? "ON" : "OFF");

  if (currentMode == DECK_CONTROLS) {
    drawAutoCueStatus();
  }
}

void handleDeckControlsMode() {
  if (touch.justPressed && isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  if (isButtonPressed(DC_COL1_X, DC_AUTOCUE_Y, DC_AUTOCUE_W, DC_AUTOCUE_H)) {
    autoCueEnabled = !autoCueEnabled;
    if (!autoCueEnabled) {
      // Leave a clean state - don't let an auto-triggered Cue stay on
      // after the feature itself has been switched off.
      setCueState(0, false);
      setCueState(1, false);
      xfZone = XF_ZONE_NONE;
      xfPendingZone = XF_ZONE_NONE;
    }
    Serial.printf("Auto Cue: %s\n", autoCueEnabled ? "ON" : "OFF");
    drawDeckControlsMode();
    return;
  }

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
