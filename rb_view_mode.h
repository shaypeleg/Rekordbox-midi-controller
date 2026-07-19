#ifndef RB_VIEW_MODE_H
#define RB_VIEW_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// 2x2 grid of panel toggle buttons
#define RBV_COL1_X 10
#define RBV_COL2_X 165
#define RBV_COL_W 145
#define RBV_BTN_H 42
#define RBV_BTN_GAP 8
#define RBV_ROW1_Y (CONTENT_Y + 8)
#define RBV_ROW2_Y (RBV_ROW1_Y + RBV_BTN_H + RBV_BTN_GAP)

// Wave Zoom slider (horizontal strip, full width)
#define RBV_ZOOM_X 20
#define RBV_ZOOM_W 280
#define RBV_ZOOM_H 50
#define RBV_ZOOM_Y (RBV_ROW2_Y + RBV_BTN_H + 28)

// Quantize zoom output: only send a new CC when the value changes by at
// least this many steps.  With STEP=4 the 280px strip yields ~32 discrete
// zoom levels instead of 128, so small finger jitter doesn't flood
// Rekordbox with rapid zoom changes.
#define RBV_ZOOM_STEP 4

// Panel toggle states
bool rbvFxPanel = false;
bool rbvSamplerPanel = false;
bool rbvMixerPanel = false;
bool rbvRecordPanel = false;

// Wave zoom state: 64 = neutral center (Rekordbox default zoom)
int rbvWaveZoom = 64;
bool rbvZoomDragging = false;

void initializeRbViewMode();
void drawRbViewMode();
void handleRbViewMode();
void drawZoomSlider();

void initializeRbViewMode() {
  rbvFxPanel = false;
  rbvSamplerPanel = false;
  rbvMixerPanel = false;
  rbvRecordPanel = false;
  rbvWaveZoom = 64;
  rbvZoomDragging = false;
}

void drawRbViewMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("RB SCREENS");

  // Row 1: FX Panel, Sampler Panel
  drawToggleButton(RBV_COL1_X, RBV_ROW1_Y, RBV_COL_W, RBV_BTN_H,
                   "FX PANEL", THEME_PRIMARY, rbvFxPanel);
  drawToggleButton(RBV_COL2_X, RBV_ROW1_Y, RBV_COL_W, RBV_BTN_H,
                   "SAMPLER", THEME_ACCENT, rbvSamplerPanel);

  // Row 2: Mixer Panel, Record Panel
  drawToggleButton(RBV_COL1_X, RBV_ROW2_Y, RBV_COL_W, RBV_BTN_H,
                   "MIXER", THEME_SUCCESS, rbvMixerPanel);
  drawToggleButton(RBV_COL2_X, RBV_ROW2_Y, RBV_COL_W, RBV_BTN_H,
                   "RECORD", THEME_ERROR, rbvRecordPanel);

  // Wave Zoom slider
  drawZoomSlider();
}

void drawZoomSlider() {
  // Label
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("WAVE ZOOM", RBV_ZOOM_X, RBV_ZOOM_Y - 16, 2);

  // "-" and "+" labels at ends
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("-", RBV_ZOOM_X - 8, RBV_ZOOM_Y + RBV_ZOOM_H / 2 - 8, 2);
  tft.drawCentreString("+", RBV_ZOOM_X + RBV_ZOOM_W + 8,
                       RBV_ZOOM_Y + RBV_ZOOM_H / 2 - 8, 2);

  // Track
  tft.fillRoundRect(RBV_ZOOM_X, RBV_ZOOM_Y, RBV_ZOOM_W, RBV_ZOOM_H, 6,
                    THEME_SURFACE);
  tft.drawRoundRect(RBV_ZOOM_X, RBV_ZOOM_Y, RBV_ZOOM_W, RBV_ZOOM_H, 6,
                    THEME_SECONDARY);

  // Center reference line
  int centerX = RBV_ZOOM_X + RBV_ZOOM_W / 2;
  tft.drawFastVLine(centerX, RBV_ZOOM_Y + 4, RBV_ZOOM_H - 8, THEME_TEXT_DIM);

  // Knob position mapped from 0-127 to the track width
  int knobX = RBV_ZOOM_X + map(rbvWaveZoom, 0, 127, 0, RBV_ZOOM_W);
  int knobW = 16;
  int knobH = RBV_ZOOM_H - 8;
  int knobY = RBV_ZOOM_Y + 4;

  tft.fillRoundRect(knobX - knobW / 2, knobY, knobW, knobH, 4,
                    THEME_SECONDARY);

  // Numeric value inside the knob
  tft.setTextColor(THEME_BG, THEME_SECONDARY);
  tft.drawCentreString(String(rbvWaveZoom), knobX, knobY + knobH / 2 - 6, 1);
}

void handleRbViewMode() {
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  // --- Wave Zoom slider (continuous drag) ---
  bool inSlider = touch.isPressed &&
                  touch.x >= RBV_ZOOM_X &&
                  touch.x <= RBV_ZOOM_X + RBV_ZOOM_W &&
                  touch.y >= RBV_ZOOM_Y &&
                  touch.y <= RBV_ZOOM_Y + RBV_ZOOM_H;

  if (inSlider) {
    int raw = map(touch.x, RBV_ZOOM_X, RBV_ZOOM_X + RBV_ZOOM_W, 0, 127);
    raw = constrain(raw, 0, 127);
    int value = (raw / RBV_ZOOM_STEP) * RBV_ZOOM_STEP;
    if (value > 127) value = 127;
    rbvZoomDragging = true;

    if (value != rbvWaveZoom) {
      rbvWaveZoom = value;
      sendMIDI(0xB0, CC_RBV_WAVE_ZOOM, value);
      drawZoomSlider();
    }
    return;
  }

  if (rbvZoomDragging && !touch.isPressed) {
    rbvZoomDragging = false;
  }

  // --- Panel toggle buttons (only on fresh tap) ---
  if (!touch.justPressed) return;

  // FX Panel
  if (isButtonPressed(RBV_COL1_X, RBV_ROW1_Y, RBV_COL_W, RBV_BTN_H)) {
    rbvFxPanel = !rbvFxPanel;
    sendToggleNote(NOTE_RBV_FX_PANEL);
    Serial.printf("RB View FX Panel: %s\n", rbvFxPanel ? "ON" : "OFF");
    drawRbViewMode();
    return;
  }

  // Sampler Panel
  if (isButtonPressed(RBV_COL2_X, RBV_ROW1_Y, RBV_COL_W, RBV_BTN_H)) {
    rbvSamplerPanel = !rbvSamplerPanel;
    sendToggleNote(NOTE_RBV_SAMPLER_PANEL);
    Serial.printf("RB View Sampler Panel: %s\n", rbvSamplerPanel ? "ON" : "OFF");
    drawRbViewMode();
    return;
  }

  // Mixer Panel
  if (isButtonPressed(RBV_COL1_X, RBV_ROW2_Y, RBV_COL_W, RBV_BTN_H)) {
    rbvMixerPanel = !rbvMixerPanel;
    sendToggleNote(NOTE_RBV_MIXER_PANEL);
    Serial.printf("RB View Mixer Panel: %s\n", rbvMixerPanel ? "ON" : "OFF");
    drawRbViewMode();
    return;
  }

  // Record Panel
  if (isButtonPressed(RBV_COL2_X, RBV_ROW2_Y, RBV_COL_W, RBV_BTN_H)) {
    rbvRecordPanel = !rbvRecordPanel;
    sendToggleNote(NOTE_RBV_RECORD_PANEL);
    Serial.printf("RB View Record Panel: %s\n", rbvRecordPanel ? "ON" : "OFF");
    drawRbViewMode();
    return;
  }
}

#endif
