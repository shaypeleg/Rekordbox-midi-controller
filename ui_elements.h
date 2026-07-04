#ifndef UI_ELEMENTS_H
#define UI_ELEMENTS_H

#include "common_definitions.h"

// Shared BACK button geometry - every mode screen exits via this same
// button, so it's defined once here instead of as a repeated magic number
// in every mode file's draw/handle functions. Sized generously (vs. the
// original 50x25) since it's the single most-tapped control in the app and
// this is a small 2.8" resistive touchscreen.
#define HEADER_H 32

// The back button's tap zone starts at the top-left corner and extends
// below the header bar. The chevron is drawn where the header meets the
// content, straddling the divider line, so it visually reads as separate
// from both. The hit area is deliberately tall (header height + overshoot)
// so an imprecise finger tap anywhere in that corner registers.
#define BACK_BTN_X 0
#define BACK_BTN_Y 0
#define BACK_BTN_W 50
#define BACK_BTN_H (HEADER_H + 14)

// Every mode screen's content should start at this Y so it clears the back
// chevron that protrudes below the header bar.
#define CONTENT_Y (HEADER_H + 16)

// UI function declarations
void updateTouch();
void updateStatus();
bool isButtonPressed(int x, int y, int w, int h);
void drawRoundButton(int x, int y, int w, int h, String text, uint16_t color, bool pressed = false);
void drawToggleButton(int x, int y, int w, int h, String text, uint16_t color, bool active);
void drawHeader(String title);
void drawToggleSlider(int x, int y, int w, int h, String label, uint16_t color, bool on);
void drawBackChevron();
void drawStatusIcons();
void drawBluetoothGlyph(int cx, int cy, uint16_t color, bool filled);
void drawWifiGlyph(int cx, int cyBottom, uint16_t color, bool filled);
void exitToMenu();

// UI implementations
void updateTouch() {
  touch.wasPressed = touch.isPressed;
  touch.isPressed = ts.tirqTouched() && ts.touched();
  touch.justPressed = touch.isPressed && !touch.wasPressed;
  touch.justReleased = !touch.isPressed && touch.wasPressed;
  
  if (touch.isPressed) {
    TS_Point p = ts.getPoint();
    touch.x = map(p.x, 200, 3700, 0, 320);
    touch.y = map(p.y, 240, 3800, 0, 240);
  }
}

bool isButtonPressed(int x, int y, int w, int h) {
  return touch.x >= x && touch.x <= x + w && touch.y >= y && touch.y <= y + h;
}

void drawRoundButton(int x, int y, int w, int h, String text, uint16_t color, bool pressed) {
  uint16_t bgColor = pressed ? color : THEME_SURFACE;
  uint16_t borderColor = color;
  uint16_t textColor = pressed ? THEME_BG : color;
  
  tft.fillRoundRect(x, y, w, h, 8, bgColor);
  tft.drawRoundRect(x, y, w, h, 8, borderColor);
  tft.drawRoundRect(x+1, y+1, w-2, h-2, 7, borderColor);
  
  tft.setTextColor(textColor, bgColor);
  tft.drawCentreString(text, x + w/2, y + h/2 - 8, 2);
}

// Persistent on/off control used by Deck Controls, Stems, and Effects
// (assign/on-off). Visual state only - Rekordbox owns the authoritative
// on/off state once a control is MIDI-mapped.
void drawToggleButton(int x, int y, int w, int h, String text, uint16_t color, bool active) {
  drawRoundButton(x, y, w, h, text, color, active);
}

// Text starts to the right of the back chevron's reserved zone instead of
// being centered on the full screen width - a centered title silently
// overlapped the chevron on longer titles ("DECK CONTROLS", "NEEDLE
// SEARCH") since its left edge landed underneath the button.
#define HEADER_TEXT_X (BACK_BTN_X + BACK_BTN_W + 6)

void drawHeader(String title) {
  tft.fillRect(0, 0, 320, HEADER_H, THEME_SURFACE);
  tft.drawFastHLine(0, HEADER_H, 320, THEME_PRIMARY);

  int availW = 320 - HEADER_TEXT_X - 6;
  int titleFont = (tft.textWidth(title, 4) <= availW) ? 4 : 2;
  int titleY = (HEADER_H - 16) / 2;
  if (titleFont == 2) titleY += 4;

  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawString(title, HEADER_TEXT_X, titleY, titleFont);

  drawBackChevron();
}

// Phone-style toggle slider: a pill-shaped track with a circular knob that
// slides left (off) or right (on). The label is drawn to the left of the
// track. Total hit area is w x h (includes the label space).
void drawToggleSlider(int x, int y, int w, int h, String label, uint16_t color, bool on) {
  int trackW = 44;
  int trackH = 20;
  int trackX = x + w - trackW;
  int trackY = y + (h - trackH) / 2;
  int knobR = trackH / 2 - 2;

  uint16_t trackColor = on ? color : THEME_TEXT_DIM;
  uint16_t fillColor = on ? color : THEME_SURFACE;

  tft.fillRoundRect(trackX, trackY, trackW, trackH, trackH / 2, fillColor);
  tft.drawRoundRect(trackX, trackY, trackW, trackH, trackH / 2, trackColor);

  int knobCx = on ? (trackX + trackW - trackH / 2) : (trackX + trackH / 2);
  int knobCy = trackY + trackH / 2;
  tft.fillCircle(knobCx, knobCy, knobR, on ? THEME_BG : THEME_TEXT_DIM);

  tft.setTextColor(on ? color : THEME_TEXT_DIM, THEME_BG);
  tft.drawString(label, x, y + h / 2 - 8, 2);
}

// Primary way to leave a screen: a quiet chevron tap target (same hit
// region as the old "BACK" pill button). Edge-swipe-to-go-back was tried
// and removed - too unreliable to register consistently on this
// resistive touchscreen.
void drawBackChevron() {
  // Centered on the header/content divider line so it visually bridges
  // the two zones and stands out from both surfaces.
  int cx = 25;
  int cy = HEADER_H;
  int r = 13;

  tft.fillCircle(cx, cy, r, THEME_BG);
  tft.drawCircle(cx, cy, r, THEME_TEXT_DIM);
  tft.drawLine(cx + 3, cy - 6, cx - 4, cy, THEME_TEXT);
  tft.drawLine(cx + 3, cy + 6, cx - 4, cy, THEME_TEXT);
}

// Bluetooth glyph: the classic rune (vertical spine + two triangles that
// meet at the badge's center point) inside a rounded-square badge, matching
// the familiar Bluetooth logo shape. `filled` true = solid brand-blue badge
// with a contrasting rune (connected); false = hollow outline badge with
// the rune drawn in the same outline color (not connected) - this is the
// exact "no connection" look requested (plain outline glyph, no fill).
void drawBluetoothGlyph(int cx, int cy, uint16_t color, bool filled) {
  int halfW = 10, halfH = 12;

  if (filled) {
    tft.fillRoundRect(cx - halfW, cy - halfH, halfW * 2, halfH * 2, 7, color);
  } else {
    tft.drawRoundRect(cx - halfW, cy - halfH, halfW * 2, halfH * 2, 7, color);
    tft.drawRoundRect(cx - halfW + 1, cy - halfH + 1, halfW * 2 - 2, halfH * 2 - 2, 6, color);
  }

  uint16_t runeColor = filled ? THEME_BG : color;
  int h = halfH - 4;
  int w = halfW - 4;
  tft.drawLine(cx, cy - h, cx, cy + h, runeColor);          // spine
  tft.drawLine(cx, cy - h, cx + w, cy - h / 2, runeColor);  // top -> upper point
  tft.drawLine(cx + w, cy - h / 2, cx, cy, runeColor);      // upper point -> center
  tft.drawLine(cx, cy, cx + w, cy + h / 2, runeColor);      // center -> lower point
  tft.drawLine(cx + w, cy + h / 2, cx, cy + h, runeColor);  // lower point -> bottom
}

// WiFi glyph: three concentric quarter-rings fanning up from a dot, like
// the familiar WiFi signal icon. `filled` true = thicker solid bands and a
// filled dot (connected); false = thin hollow rings and a hollow dot (not
// connected). Uses TFT_eSPI's drawArc (0deg = 6 o'clock, clockwise), so
// 90->270 sweeps through 12 o'clock = the upper half only.
void drawWifiGlyph(int cx, int cyBottom, uint16_t color, bool filled) {
  if (filled) {
    tft.fillCircle(cx, cyBottom, 2, color);
  } else {
    tft.drawCircle(cx, cyBottom, 2, color);
  }

  int outerRadii[3] = {4, 7, 10};
  int thickness = filled ? 2 : 1;
  for (int i = 0; i < 3; i++) {
    int outer = outerRadii[i];
    int inner = outer - thickness;
    tft.drawArc(cx, cyBottom, outer, inner, 90, 270, color, THEME_SURFACE, true);
  }
}

// Bluetooth / WiFi status badges, top-right of the main menu. Solid/filled
// means connected; a hollow outline means not connected (WiFi also shows
// an amber outline while a connection attempt is in progress). Bluetooth
// always uses brand blue for "connected" so it reads at a glance, distinct
// from WiFi's green.
void drawStatusIcons() {
  int btX = 260, btY = 27;
  int wifiX = 298, wifiY = 38;

  drawBluetoothGlyph(btX, btY, deviceConnected ? BLUETOOTH_BLUE : THEME_TEXT_DIM, deviceConnected);

  uint16_t wifiColor = wifiConnected ? THEME_SUCCESS : (wifiConnecting ? THEME_WARNING : THEME_TEXT_DIM);
  drawWifiGlyph(wifiX, wifiY, wifiColor, wifiConnected);
}

void updateStatus() {
  // Status bar removed - no more BLE connection alerts on every screen
}

#endif
