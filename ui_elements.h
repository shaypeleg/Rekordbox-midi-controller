#ifndef UI_ELEMENTS_H
#define UI_ELEMENTS_H

#include "common_definitions.h"
#include "icons.h"

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
void drawBluetoothGlyph(int cx, int cy, uint16_t color);
void drawWifiGlyph(int cx, int cy, uint16_t color);
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
// overlapped the chevron on longer titles ("DECK CONTROLS", "SONG
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

// Bluetooth glyph drawn from XBM bitmap (icons.h). Thick-stroke
// bind-rune, no surrounding badge. Color alone indicates state:
// BLUETOOTH_BLUE = connected, THEME_TEXT_DIM = not connected.
void drawBluetoothGlyph(int cx, int cy, uint16_t color) {
  int bmpX = cx - BT_RUNE_W / 2;
  int bmpY = cy - BT_RUNE_H / 2;
  tft.fillRect(bmpX, bmpY, BT_RUNE_W, BT_RUNE_H, THEME_SURFACE);
  tft.drawXBitmap(bmpX, bmpY, bt_rune_bits, BT_RUNE_W, BT_RUNE_H, color);
}

// WiFi glyph drawn from XBM bitmap (icons.h). Three thick filled arc
// bands fanning upward. Color indicates state: THEME_SUCCESS = connected,
// THEME_WARNING = connecting, THEME_TEXT_DIM = not connected.
void drawWifiGlyph(int cx, int cy, uint16_t color) {
  int bmpX = cx - WIFI_ICON_W / 2;
  int bmpY = cy - WIFI_ICON_H / 2;
  tft.fillRect(bmpX, bmpY, WIFI_ICON_W, WIFI_ICON_H, THEME_SURFACE);
  tft.drawXBitmap(bmpX, bmpY, wifi_icon_bits, WIFI_ICON_W, WIFI_ICON_H, color);
}

// Bluetooth / WiFi status icons, top-right of the main menu. Both icons
// are vertically centered at the same Y so they read as a balanced pair.
// Color encodes connection state; shape is always the same.
void drawStatusIcons() {
  int iconCY = 27;

  drawBluetoothGlyph(260, iconCY, deviceConnected ? BLUETOOTH_BLUE : THEME_TEXT_DIM);

  uint16_t wifiColor = wifiConnected ? THEME_SUCCESS : (wifiConnecting ? THEME_WARNING : THEME_TEXT_DIM);
  drawWifiGlyph(295, iconCY, wifiColor);
}

void updateStatus() {
  // Status bar removed - no more BLE connection alerts on every screen
}

#endif
