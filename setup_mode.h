#ifndef SETUP_MODE_H
#define SETUP_MODE_H

#include <ctype.h>
#include "common_definitions.h"
#include "ui_elements.h"
#include "wifi_manager.h"
#include "theme_manager.h"

// The Setup screen is a small internal flow: Home -> WiFi scan list ->
// on-screen keyboard (password entry) -> back to Home.
enum SetupScreen {
  SETUP_HOME,
  SETUP_WIFI_SCAN,
  SETUP_WIFI_KEYBOARD
};

SetupScreen setupScreen = SETUP_HOME;
WiFiNetwork setupScanResults[WIFI_MAX_SCAN_RESULTS];
int setupScanCount = 0;
String setupSelectedSSID = "";
String setupPasswordInput = "";
bool setupShowPassword = false;
bool setupShiftActive = false;
bool setupSymbolsActive = false;
#define SETUP_MAX_PASSWORD_LEN 32

// Function declarations
void initializeSetupMode();
void drawSetupMode();
void handleSetupMode();
void drawSetupHome();
void handleSetupHome();
void startWifiScan();
void drawSetupWifiScan();
void handleSetupWifiScan();
void drawSetupKeyboard();
void handleSetupKeyboard();

void initializeSetupMode() {
  setupScreen = SETUP_HOME;
}

void drawSetupMode() {
  switch (setupScreen) {
    case SETUP_HOME:         drawSetupHome();   break;
    case SETUP_WIFI_SCAN:    drawSetupWifiScan(); break;
    case SETUP_WIFI_KEYBOARD: drawSetupKeyboard(); break;
  }
}

void handleSetupMode() {
  switch (setupScreen) {
    case SETUP_HOME:         handleSetupHome();   break;
    case SETUP_WIFI_SCAN:    handleSetupWifiScan(); break;
    case SETUP_WIFI_KEYBOARD: handleSetupKeyboard(); break;
  }
}

// --- Home: Bluetooth status + WiFi status ---

void drawSetupHome() {
  tft.fillScreen(THEME_BG);
  drawHeader("SETUP");

  // Version lives here instead of the main menu.
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawRightString(APP_VERSION, 312, 10, 1);

  // --- Bluetooth ---
  int sy = CONTENT_Y;
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawString("BLUETOOTH", 10, sy, 2);

  tft.setTextColor(deviceConnected ? THEME_SUCCESS : THEME_TEXT_DIM, THEME_BG);
  tft.drawString(deviceConnected ? "Connected" : "Waiting for connection...", 10, sy + 20, 1);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Device name: RB-MIDI", 10, sy + 34, 1);

  drawRoundButton(210, sy - 2, 100, 28, "RESTART", THEME_SECONDARY);

  tft.drawFastHLine(10, sy + 50, 300, THEME_SURFACE);

  // --- WiFi ---
  int wy = sy + 56;
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawString("WI-FI", 10, wy, 2);

  String wifiStatus;
  uint16_t wifiStatusColor;
  if (wifiConnected) {
    wifiStatus = "Connected: " + currentSSID;
    wifiStatusColor = THEME_SUCCESS;
  } else if (wifiConnecting) {
    wifiStatus = "Connecting to " + currentSSID + "...";
    wifiStatusColor = THEME_WARNING;
  } else {
    wifiStatus = "Not connected";
    wifiStatusColor = THEME_TEXT_DIM;
  }
  tft.setTextColor(wifiStatusColor, THEME_BG);
  tft.drawString(wifiStatus, 10, wy + 20, 1);

  drawRoundButton(10, wy + 36, 145, 32, "SCAN NETWORKS", THEME_PRIMARY);
  drawRoundButton(165, wy + 36, 145, 32, "FORGET NETWORK", THEME_ERROR);

  tft.drawFastHLine(10, wy + 76, 300, THEME_SURFACE);

  // --- Display theme ---
  int dy = wy + 82;
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawString("DISPLAY", 10, dy, 2);

  drawToggleButton(150, dy - 2, 75, 32, "DARK", THEME_PRIMARY, darkMode);
  drawToggleButton(230, dy - 2, 75, 32, "LIGHT", THEME_WARNING, !darkMode);
}

void handleSetupHome() {
  if (!touch.justPressed) return;

  if (isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }
  int sy = CONTENT_Y;
  int wy = sy + 56;
  int dy = wy + 82;
  if (isButtonPressed(210, sy - 2, 100, 28)) {
    BLEDevice::startAdvertising();
    drawSetupHome();
    return;
  }
  if (isButtonPressed(10, wy + 36, 145, 32)) {
    startWifiScan();
    return;
  }
  if (isButtonPressed(165, wy + 36, 145, 32)) {
    forgetNetwork();
    drawSetupHome();
    return;
  }
  if (isButtonPressed(150, dy - 2, 75, 32)) {
    if (!darkMode) toggleTheme();
    drawSetupHome();
    return;
  }
  if (isButtonPressed(230, dy - 2, 75, 32)) {
    if (darkMode) toggleTheme();
    drawSetupHome();
    return;
  }
}

// --- WiFi scan results ---

#define SETUP_SCAN_ROW_H 30
#define SETUP_SCAN_ROW_GAP 5
#define SETUP_SCAN_Y0 38
#define SETUP_RESCAN_Y 214
#define SETUP_RESCAN_H 24

void startWifiScan() {
  setupScreen = SETUP_WIFI_SCAN;
  tft.fillScreen(THEME_BG);
  drawHeader("SELECT NETWORK");
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("Scanning...", 160, HEADER_H + 6, 1);
  setupScanCount = scanNetworks(setupScanResults);
  drawSetupWifiScan();
}

void drawSetupWifiScan() {
  tft.fillScreen(THEME_BG);
  drawHeader("SELECT NETWORK");

  for (int i = 0; i < setupScanCount; i++) {
    int y = SETUP_SCAN_Y0 + i * (SETUP_SCAN_ROW_H + SETUP_SCAN_ROW_GAP);
    drawRoundButton(10, y, 300, SETUP_SCAN_ROW_H, setupScanResults[i].ssid, THEME_PRIMARY);
  }

  drawRoundButton(10, SETUP_RESCAN_Y, 300, SETUP_RESCAN_H, "RESCAN", THEME_SECONDARY);
}

void handleSetupWifiScan() {
  if (!touch.justPressed) return;

  if (isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    setupScreen = SETUP_HOME;
    drawSetupHome();
    return;
  }
  if (isButtonPressed(10, SETUP_RESCAN_Y, 300, SETUP_RESCAN_H)) {
    startWifiScan();
    return;
  }

  for (int i = 0; i < setupScanCount; i++) {
    int y = SETUP_SCAN_Y0 + i * (SETUP_SCAN_ROW_H + SETUP_SCAN_ROW_GAP);
    if (isButtonPressed(10, y, 300, SETUP_SCAN_ROW_H)) {
      setupSelectedSSID = setupScanResults[i].ssid;
      setupPasswordInput = "";
      setupShowPassword = false;
      setupShiftActive = false;
      setupSymbolsActive = false;
      setupScreen = SETUP_WIFI_KEYBOARD;
      drawSetupKeyboard();
      return;
    }
  }
}

// --- On-screen keyboard (WiFi password entry) ---

const char SETUP_KB_ROW1_LETTERS[10] = {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'};
const char SETUP_KB_ROW2_LETTERS[9]  = {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l'};
const char SETUP_KB_ROW3_LETTERS[7]  = {'z', 'x', 'c', 'v', 'b', 'n', 'm'};

const char SETUP_KB_ROW1_SYMBOLS[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
const char SETUP_KB_ROW2_SYMBOLS[9]  = {'-', '_', '!', '@', '#', '$', '%', '^', '&'};
const char SETUP_KB_ROW3_SYMBOLS[7]  = {'*', '(', ')', '+', '=', ':', '.'};

#define KB_KEY_W 32
#define KB_KEY_H 33
#define KB_ROW1_Y 66
#define KB_ROW2_Y 102
#define KB_ROW3_Y 138
#define KB_ROW4_Y 174
#define KB_ROW4_H 34

void drawSetupKeyboard() {
  tft.fillScreen(THEME_BG);
  drawHeader("WI-FI PASSWORD");

  tft.fillRoundRect(10, 38, 250, 24, 6, THEME_SURFACE);
  tft.drawRoundRect(10, 38, 250, 24, 6, THEME_PRIMARY);
  String masked = "";
  for (unsigned int i = 0; i < setupPasswordInput.length(); i++) masked += "*";
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawString(setupShowPassword ? setupPasswordInput : masked, 16, 44, 2);

  drawRoundButton(265, 38, 45, 24, setupShowPassword ? "HIDE" : "SHOW", THEME_ACCENT);

  bool symbols = setupSymbolsActive;

  // Row 1 - 10 keys
  for (int i = 0; i < 10; i++) {
    char c = symbols ? SETUP_KB_ROW1_SYMBOLS[i] : SETUP_KB_ROW1_LETTERS[i];
    if (!symbols && setupShiftActive) c = toupper(c);
    drawRoundButton(i * KB_KEY_W, KB_ROW1_Y, KB_KEY_W - 2, KB_KEY_H, String(c), THEME_TEXT);
  }

  // Row 2 - 9 keys, indented half a key (classic QWERTY stagger)
  int row2X = KB_KEY_W / 2;
  for (int i = 0; i < 9; i++) {
    char c = symbols ? SETUP_KB_ROW2_SYMBOLS[i] : SETUP_KB_ROW2_LETTERS[i];
    if (!symbols && setupShiftActive) c = toupper(c);
    drawRoundButton(row2X + i * KB_KEY_W, KB_ROW2_Y, KB_KEY_W - 2, KB_KEY_H, String(c), THEME_TEXT);
  }

  // Row 3 - SHIFT | 7 keys | DEL
  drawToggleButton(0, KB_ROW3_Y, 56, KB_KEY_H, "SHIFT", THEME_ACCENT, setupShiftActive);
  for (int i = 0; i < 7; i++) {
    char c = symbols ? SETUP_KB_ROW3_SYMBOLS[i] : SETUP_KB_ROW3_LETTERS[i];
    if (!symbols && setupShiftActive) c = toupper(c);
    drawRoundButton(56 + i * KB_KEY_W, KB_ROW3_Y, KB_KEY_W - 2, KB_KEY_H, String(c), THEME_TEXT);
  }
  int delX = 56 + 7 * KB_KEY_W;
  drawRoundButton(delX, KB_ROW3_Y, 320 - delX, KB_KEY_H, "DEL", THEME_ERROR);

  // Row 4 - mode toggle | space | connect
  drawRoundButton(0, KB_ROW4_Y, 70, KB_ROW4_H, symbols ? "ABC" : "123", THEME_SECONDARY);
  drawRoundButton(70, KB_ROW4_Y, 180, KB_ROW4_H, "SPACE", THEME_TEXT_DIM);
  drawRoundButton(250, KB_ROW4_Y, 70, KB_ROW4_H, "GO", THEME_SUCCESS);
}

void handleSetupKeyboard() {
  if (!touch.justPressed) return;

  if (isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    setupScreen = SETUP_WIFI_SCAN;
    drawSetupWifiScan();
    return;
  }

  if (isButtonPressed(265, 38, 45, 24)) {
    setupShowPassword = !setupShowPassword;
    drawSetupKeyboard();
    return;
  }

  bool symbols = setupSymbolsActive;

  for (int i = 0; i < 10; i++) {
    if (isButtonPressed(i * KB_KEY_W, KB_ROW1_Y, KB_KEY_W - 2, KB_KEY_H)) {
      char c = symbols ? SETUP_KB_ROW1_SYMBOLS[i] : SETUP_KB_ROW1_LETTERS[i];
      if (!symbols && setupShiftActive) c = toupper(c);
      if (setupPasswordInput.length() < SETUP_MAX_PASSWORD_LEN) setupPasswordInput += c;
      drawSetupKeyboard();
      return;
    }
  }

  int row2X = KB_KEY_W / 2;
  for (int i = 0; i < 9; i++) {
    if (isButtonPressed(row2X + i * KB_KEY_W, KB_ROW2_Y, KB_KEY_W - 2, KB_KEY_H)) {
      char c = symbols ? SETUP_KB_ROW2_SYMBOLS[i] : SETUP_KB_ROW2_LETTERS[i];
      if (!symbols && setupShiftActive) c = toupper(c);
      if (setupPasswordInput.length() < SETUP_MAX_PASSWORD_LEN) setupPasswordInput += c;
      drawSetupKeyboard();
      return;
    }
  }

  if (isButtonPressed(0, KB_ROW3_Y, 56, KB_KEY_H)) {
    setupShiftActive = !setupShiftActive;
    drawSetupKeyboard();
    return;
  }
  for (int i = 0; i < 7; i++) {
    if (isButtonPressed(56 + i * KB_KEY_W, KB_ROW3_Y, KB_KEY_W - 2, KB_KEY_H)) {
      char c = symbols ? SETUP_KB_ROW3_SYMBOLS[i] : SETUP_KB_ROW3_LETTERS[i];
      if (!symbols && setupShiftActive) c = toupper(c);
      if (setupPasswordInput.length() < SETUP_MAX_PASSWORD_LEN) setupPasswordInput += c;
      drawSetupKeyboard();
      return;
    }
  }
  int delX = 56 + 7 * KB_KEY_W;
  if (isButtonPressed(delX, KB_ROW3_Y, 320 - delX, KB_KEY_H)) {
    if (setupPasswordInput.length() > 0) setupPasswordInput.remove(setupPasswordInput.length() - 1);
    drawSetupKeyboard();
    return;
  }

  if (isButtonPressed(0, KB_ROW4_Y, 70, KB_ROW4_H)) {
    setupSymbolsActive = !setupSymbolsActive;
    drawSetupKeyboard();
    return;
  }
  if (isButtonPressed(70, KB_ROW4_Y, 180, KB_ROW4_H)) {
    if (setupPasswordInput.length() < SETUP_MAX_PASSWORD_LEN) setupPasswordInput += ' ';
    drawSetupKeyboard();
    return;
  }
  if (isButtonPressed(250, KB_ROW4_Y, 70, KB_ROW4_H)) {
    tft.fillScreen(THEME_BG);
    drawHeader("WI-FI PASSWORD");
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString("Connecting...", 160, 120, 2);
    connectToNetwork(setupSelectedSSID, setupPasswordInput);
    setupScreen = SETUP_HOME;
    drawSetupHome();
    return;
  }
}

#endif
