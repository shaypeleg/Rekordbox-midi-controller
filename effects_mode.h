#ifndef EFFECTS_MODE_H
#define EFFECTS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Rekordbox's MIDI Learn only exposes a "move up the FX list" function out
// of the box (FX1-1Select). We emulate a rotary encoder with a circular drag
// gesture: every FX_TICK_THRESHOLD degrees of rotation fires one NEXT/PREV
// tick, so the knob feels continuous even though it drives a discrete list.
#define FX_KNOB_CX 160
#define FX_KNOB_CY 110
#define FX_KNOB_R 52
#define FX_KNOB_TOUCH_R 78
#define FX_TICK_THRESHOLD 22.0

bool fxOn = false;
bool fxAssignD1 = false;
bool fxAssignD2 = false;
bool fxKnobDragging = false;
float fxLastAngle = 0;
float fxKnobVisualAngle = 0;
float fxTickAccumulator = 0;

// Function declarations
void initializeEffectsMode();
void drawEffectsMode();
void handleEffectsMode();
void drawFxKnob();
void drawFxControls();
float fxAngleForTouch(int x, int y);

void initializeEffectsMode() {
  fxOn = false;
  fxAssignD1 = false;
  fxAssignD2 = false;
  fxKnobDragging = false;
  fxKnobVisualAngle = 0;
  fxTickAccumulator = 0;
}

void drawEffectsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("EFFECTS");
  drawFxKnob();
  drawFxControls();
}

void drawFxKnob() {
  tft.fillCircle(FX_KNOB_CX, FX_KNOB_CY, FX_KNOB_R, THEME_SURFACE);
  tft.drawCircle(FX_KNOB_CX, FX_KNOB_CY, FX_KNOB_R, THEME_PRIMARY);
  tft.drawCircle(FX_KNOB_CX, FX_KNOB_CY, FX_KNOB_R - 1, THEME_PRIMARY);

  float rad = fxKnobVisualAngle * PI / 180.0;
  int px = FX_KNOB_CX + (int)((FX_KNOB_R - 8) * cos(rad));
  int py = FX_KNOB_CY + (int)((FX_KNOB_R - 8) * sin(rad));
  tft.drawLine(FX_KNOB_CX, FX_KNOB_CY, px, py, THEME_ACCENT);
  tft.fillCircle(FX_KNOB_CX, FX_KNOB_CY, 6, THEME_ACCENT);
}

void drawFxControls() {
  drawToggleButton(10, 176, 145, 30, "DECK 1", THEME_SECONDARY, fxAssignD1);
  drawToggleButton(165, 176, 145, 30, "DECK 2", THEME_SECONDARY, fxAssignD2);
  drawToggleButton(85, 210, 150, 28, "FX ON/OFF", THEME_WARNING, fxOn);
}

float fxAngleForTouch(int x, int y) {
  return atan2((float)(y - FX_KNOB_CY), (float)(x - FX_KNOB_CX)) * 180.0 / PI;
}

void handleEffectsMode() {
  if (touch.justPressed && isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    if (isButtonPressed(10, 176, 145, 30)) {
      fxAssignD1 = !fxAssignD1;
      sendToggleNote(NOTE_FX_ASSIGN_D1);
      drawFxControls();
      return;
    }
    if (isButtonPressed(165, 176, 145, 30)) {
      fxAssignD2 = !fxAssignD2;
      sendToggleNote(NOTE_FX_ASSIGN_D2);
      drawFxControls();
      return;
    }
    if (isButtonPressed(85, 210, 150, 28)) {
      fxOn = !fxOn;
      sendToggleNote(NOTE_FX_ONOFF);
      drawFxControls();
      return;
    }
  }

  // Rotary knob gesture - only while dragging within the knob's touch radius
  int dx = touch.x - FX_KNOB_CX;
  int dy = touch.y - FX_KNOB_CY;
  bool nearKnob = (dx * dx + dy * dy) <= (FX_KNOB_TOUCH_R * FX_KNOB_TOUCH_R);

  if (touch.isPressed && nearKnob) {
    float angle = fxAngleForTouch(touch.x, touch.y);

    if (!fxKnobDragging) {
      fxKnobDragging = true;
      fxLastAngle = angle;
      return;
    }

    float delta = angle - fxLastAngle;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;

    fxKnobVisualAngle += delta;
    fxTickAccumulator += delta;
    fxLastAngle = angle;

    while (fxTickAccumulator >= FX_TICK_THRESHOLD) {
      sendToggleNote(NOTE_FX_NEXT);
      Serial.println("FX NEXT");
      fxTickAccumulator -= FX_TICK_THRESHOLD;
    }
    while (fxTickAccumulator <= -FX_TICK_THRESHOLD) {
      sendToggleNote(NOTE_FX_PREV);
      Serial.println("FX PREV");
      fxTickAccumulator += FX_TICK_THRESHOLD;
    }

    drawFxKnob();
  } else if (fxKnobDragging) {
    fxKnobDragging = false;
  }
}

#endif
