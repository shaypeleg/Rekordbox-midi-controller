/*******************************************************************
 DJ Bluetooth MIDI Controller for ESP32 Cheap Yellow Display
 Main file - handles setup, menu, and screen switching
 *******************************************************************/

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <NimBLEDevice.h>

// Include screen files
#include "wifi_manager.h"
#include "theme_manager.h"
#include "effects_mode.h"
#include "deck_controls_mode.h"
#include "needle_search_mode.h"
#include "stems_mode.h"
#include "rb_view_mode.h"
#include "setup_mode.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Hardware setup
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Global objects
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// BLE MIDI globals
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
uint8_t midiPacket[] = {0x80, 0x80, 0x00, 0x60, 0x7F};

// WiFi globals - managed by wifi_manager.h
bool wifiConnected = false;
bool wifiConnecting = false;
String currentSSID = "";

// Touch state
TouchState touch;

// App state
AppMode currentMode = MENU;

// Forward declarations
void drawMenu();

// Scalable App Icon System
// To add new screens:
// 1. Add new mode to AppMode enum in common_definitions.h
// 2. Create mode header file (e.g., new_mode.h)
// 3. Include header in this file
// 4. Add to initialization, loop, and enterMode switch statements
// 5. Add entry to apps[] array below
// 6. Add graphics case to drawAppGraphics() function
struct AppIcon {
  String name;
  uint16_t color;
  AppMode mode;
};

// SETUP is intentionally not in this list - it configures the device
// itself rather than controlling Rekordbox, so it lives as a small gear
// icon in the corner of the menu instead of a full-size function button.
#define MAX_APPS 12
AppIcon apps[] = {
  {"FX", 0xF800, EFFECTS},           // Red
  {"DECKS", 0x07E0, DECK_CONTROLS},  // Green
  {"SEARCH", 0x001F, NEEDLE_SEARCH}, // Blue
  {"STEMS", 0x781F, STEMS},          // Purple
  {"VIEW", 0xFDA0, RB_VIEW},         // Orange
};

int numApps = 5;

// Small settings shortcut, bottom-right of the menu. Sized smaller than the
// main function icons (it's a secondary, infrequent action) but still a
// comfortable tap target.
#define MENU_GEAR_SIZE 40
#define MENU_GEAR_X (320 - 12 - MENU_GEAR_SIZE)
#define MENU_GEAR_Y (240 - 12 - MENU_GEAR_SIZE)

// Function grid geometry - shared by drawMenu() and handleMenuTouch() so
// the tap targets can never drift out of sync with what's drawn.
#define MENU_HEADER_H 54
#define MENU_ICON_SIZE 52
#define MENU_ICON_SPACING 10
#define MENU_GRID_W (numApps * MENU_ICON_SIZE + (numApps - 1) * MENU_ICON_SPACING)
#define MENU_GRID_X ((320 - MENU_GRID_W) / 2)
#define MENU_GRID_BLOCK_H (MENU_ICON_SIZE + 5 + 10)
#define MENU_GRID_Y (MENU_HEADER_H + ((MENU_GEAR_Y - 8) - MENU_HEADER_H - MENU_GRID_BLOCK_H) / 2)

// BLE callbacks run in the NimBLE host task - NOT the Arduino loop.
// Touching SPI (TFT), calling notify(), or anything that takes a FreeRTOS
// mutex here causes an xTaskPriorityDisinherit assertion crash. So we only
// set flags and let loop() do the actual work.
volatile bool bleJustConnected = false;
volatile bool bleJustDisconnected = false;
BLEServer *pServer = nullptr;

class MIDICallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, NimBLEConnInfo& connInfo) override {
      deviceConnected = true;
      bleJustConnected = true;
    }
    void onDisconnect(BLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
      deviceConnected = false;
      bleJustDisconnected = true;
    }
};

void setBackLED(bool r, bool g, bool b) {
  digitalWrite(LED_R_PIN, r ? LOW : HIGH);
  digitalWrite(LED_G_PIN, g ? LOW : HIGH);
  digitalWrite(LED_B_PIN, b ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);

  // RGB LED on the back of the board
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  setBackLED(false, false, false);

  // Load saved dark/light preference before the first screen is drawn
  initThemeManager();

  // Touch setup
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);
  
  // Display setup
  tft.init();
  tft.setRotation(1);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  
  // BLE MIDI Setup
  Serial.println("Initializing BLE MIDI...");
  BLEDevice::init("RB-MIDI");
  Serial.println("BLE Device initialized");
  
  BLEServer *server = BLEDevice::createServer();
  pServer = server;
  server->setCallbacks(new MIDICallbacks());
  Serial.println("BLE Server created");
  
  BLEService *service = server->createService(BLEUUID(SERVICE_UUID));
  Serial.println("BLE Service created");
  
  pCharacteristic = service->createCharacteristic(
    BLEUUID(CHARACTERISTIC_UUID),
    NIMBLE_PROPERTY::READ |
    NIMBLE_PROPERTY::WRITE |
    NIMBLE_PROPERTY::NOTIFY
  );
  // NimBLE automatically creates the 0x2902 (CCCD) descriptor for
  // characteristics with the NOTIFY property, so no manual BLE2902 needed.
  service->start();
  Serial.println("BLE Service started");
  
  BLEAdvertising *advertising = server->getAdvertising();
  advertising->addServiceUUID(service->getUUID());
  BLEAdvertisementData adData;
  adData.setName("RB-MIDI");
  adData.setCompleteServices(BLEUUID(SERVICE_UUID));
  advertising->setAdvertisementData(adData);
  // Scan response carries the name a second time so macOS can match the
  // device identity on reconnect even if it missed the primary ADV packet.
  BLEAdvertisementData scanData;
  scanData.setName("RB-MIDI");
  advertising->setScanResponseData(scanData);
  advertising->setPreferredParams(0x06, 0x12);
  advertising->start();
  Serial.println("BLE Advertising started - Device discoverable as 'RB-MIDI'");
  
  // WiFi setup - non-blocking, reconnects to any saved network
  initWiFiManager();
  
  // Initialize screen state
  initializeEffectsMode();
  initializeDeckControlsMode();
  initializeNeedleSearchMode();
  initializeStemsMode();
  initializeRbViewMode();
  initializeSetupMode();
  
  drawMenu();
  updateStatus();
  Serial.println("DJ MIDI Controller ready!");
}

void loop() {
  // Handle BLE connect/disconnect events deferred from the NimBLE task.
  // Safe to do SPI/TFT/notify work here since we're in the Arduino loop.
  if (bleJustConnected) {
    bleJustConnected = false;
    Serial.println("BLE connected");
    setBackLED(false, false, true);
    if (currentMode == MENU) drawMenu();
    updateStatus();
  }
  if (bleJustDisconnected) {
    bleJustDisconnected = false;
    Serial.println("BLE disconnected - restarting advertising");
    setBackLED(false, false, false);
    if (currentMode == MENU) drawMenu();
    updateStatus();
    delay(100);
    pServer->startAdvertising();
  }

  // Tracks the last-drawn connection states so the menu's status icons
  // refresh the instant WiFi/BLE actually connect, instead of only
  // catching up the next time the menu happens to redraw (e.g. after
  // leaving and re-entering another screen).
  static bool lastWifiConnected = false;
  static bool lastWifiConnecting = false;
  static bool lastDeviceConnected = false;

  updateTouch();
  updateWiFiManager();

  if (currentMode == MENU &&
      (wifiConnected != lastWifiConnected ||
       wifiConnecting != lastWifiConnecting ||
       deviceConnected != lastDeviceConnected)) {
    drawStatusIcons();
  }
  lastWifiConnected = wifiConnected;
  lastWifiConnecting = wifiConnecting;
  lastDeviceConnected = deviceConnected;

  switch (currentMode) {
    case MENU:
      if (touch.justPressed) handleMenuTouch();
      break;
    case EFFECTS:
      handleEffectsMode();
      break;
    case DECK_CONTROLS:
      handleDeckControlsMode();
      break;
    case NEEDLE_SEARCH:
      handleNeedleSearchMode();
      break;
    case STEMS:
      handleStemsMode();
      break;
    case RB_VIEW:
      handleRbViewMode();
      break;
    case SETUP:
      handleSetupMode();
      break;
  }
  
  delay(20);
}

void drawMenu() {
  tft.fillScreen(THEME_BG);
  
  // Header - taller than a mode screen's (54 vs 45) so the two-line
  // title/subtitle get real breathing room instead of the subtitle
  // crowding straight into the title's descenders.
  tft.fillRect(0, 0, 320, MENU_HEADER_H, THEME_SURFACE);
  tft.drawFastHLine(0, MENU_HEADER_H, 320, THEME_PRIMARY);
  tft.setTextColor(THEME_PRIMARY, THEME_SURFACE);
  tft.drawCentreString("DJ CONTROL", 150, 7, 4);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawCentreString("Bluetooth MIDI for Rekordbox", 150, 38, 1);
  
  // Bluetooth/WiFi status icons
  drawStatusIcons();
  
  // Grid layout, centered in the space between the header and the gear
  // icon. Only the four controller functions live here now - Setup moved
  // to its own icon since it configures the device, not Rekordbox.
  for (int i = 0; i < numApps; i++) {
    int x = MENU_GRID_X + i * (MENU_ICON_SIZE + MENU_ICON_SPACING);
    int y = MENU_GRID_Y;
    
    uint16_t iconColor = apps[i].color;
    
    tft.fillRoundRect(x, y, MENU_ICON_SIZE, MENU_ICON_SIZE, 8, iconColor);
    tft.drawRoundRect(x, y, MENU_ICON_SIZE, MENU_ICON_SIZE, 8, THEME_TEXT);
    
    // Draw screen-specific graphics
    drawAppGraphics(apps[i].mode, x, y, MENU_ICON_SIZE);
    
    // Screen name
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString(apps[i].name, x + MENU_ICON_SIZE/2, y + MENU_ICON_SIZE + 5, 1);
  }
  
  drawMenuGearButton();
}

// Quiet settings shortcut - an outline badge (not a bright filled button)
// so it visually reads as secondary/utility, distinct from the four
// colorful controller function icons.
void drawMenuGearButton() {
  int cx = MENU_GEAR_X + MENU_GEAR_SIZE / 2;
  int cy = MENU_GEAR_Y + MENU_GEAR_SIZE / 2;
  uint16_t c = THEME_TEXT_DIM;

  // Inner hub
  tft.drawCircle(cx, cy, 4, c);
  tft.drawCircle(cx, cy, 3, c);

  // Body ring
  int rInner = 8;
  int rOuter = 11;
  tft.drawCircle(cx, cy, rInner, c);
  tft.drawCircle(cx, cy, rOuter, c);

  // 8 teeth: thick rectangular nubs protruding from the body ring,
  // drawn as filled quads between rOuter and rTooth at each tooth angle.
  int numTeeth = 8;
  int rTooth = 15;
  float toothHalfW = 14.0;  // angular half-width in degrees
  for (int i = 0; i < numTeeth; i++) {
    float centerAngle = i * 360.0 / numTeeth;
    float a1 = (centerAngle - toothHalfW) * PI / 180.0;
    float a2 = (centerAngle + toothHalfW) * PI / 180.0;

    // Four corners of the tooth rectangle
    int ix1 = cx + (int)(rOuter * cos(a1));
    int iy1 = cy + (int)(rOuter * sin(a1));
    int ix2 = cx + (int)(rOuter * cos(a2));
    int iy2 = cy + (int)(rOuter * sin(a2));
    int ox1 = cx + (int)(rTooth * cos(a1));
    int oy1 = cy + (int)(rTooth * sin(a1));
    int ox2 = cx + (int)(rTooth * cos(a2));
    int oy2 = cy + (int)(rTooth * sin(a2));

    tft.drawLine(ix1, iy1, ox1, oy1, c);
    tft.drawLine(ox1, oy1, ox2, oy2, c);
    tft.drawLine(ox2, oy2, ix2, iy2, c);
  }
}

void drawAppGraphics(AppMode mode, int x, int y, int iconSize) {
  int centerX = x + iconSize / 2;
  int centerY = y + iconSize / 2;

  switch (mode) {
    case EFFECTS: // knob with pointer
      {
        tft.drawCircle(centerX, centerY, 10, THEME_BG);
        tft.drawLine(centerX, centerY, centerX + 7, centerY - 7, THEME_BG);
      }
      break;
    case DECK_CONTROLS: // two mini faders
      {
        tft.drawFastVLine(centerX - 6, centerY - 10, 20, THEME_BG);
        tft.fillRect(centerX - 8, centerY - 2, 4, 3, THEME_BG);
        tft.drawFastVLine(centerX + 6, centerY - 10, 20, THEME_BG);
        tft.fillRect(centerX + 4, centerY + 3, 4, 3, THEME_BG);
      }
      break;
    case NEEDLE_SEARCH: // magnifying glass
      {
        tft.drawCircle(centerX - 2, centerY - 2, 8, THEME_BG);
        tft.drawLine(centerX + 4, centerY + 4, centerX + 10, centerY + 10, THEME_BG);
      }
      break;
    case STEMS: // stacked layers
      {
        for (int i = 0; i < 3; i++) {
          tft.fillRect(centerX - 10, centerY - 9 + i * 7, 20, 4, THEME_BG);
        }
      }
      break;
    case RB_VIEW: // monitor with panels
      {
        tft.drawRect(centerX - 10, centerY - 8, 20, 14, THEME_BG);
        tft.drawFastVLine(centerX, centerY - 8, 14, THEME_BG);
        tft.drawFastHLine(centerX - 10, centerY - 1, 20, THEME_BG);
        tft.drawFastVLine(centerX, centerY + 6, 5, THEME_BG);
        tft.drawFastHLine(centerX - 4, centerY + 10, 8, THEME_BG);
      }
      break;
    default:
      break; // SETUP has its own gear badge (drawMenuGearButton) - not part of this grid
  }
}

void handleMenuTouch() {
  if (isButtonPressed(MENU_GEAR_X, MENU_GEAR_Y, MENU_GEAR_SIZE, MENU_GEAR_SIZE)) {
    enterMode(SETUP);
    return;
  }

  for (int i = 0; i < numApps; i++) {
    int x = MENU_GRID_X + i * (MENU_ICON_SIZE + MENU_ICON_SPACING);
    
    if (isButtonPressed(x, MENU_GRID_Y, MENU_ICON_SIZE, MENU_ICON_SIZE)) {
      enterMode(apps[i].mode);
      return;
    }
  }
}

void enterMode(AppMode mode) {
  currentMode = mode;
  switch (mode) {
    case EFFECTS:
      drawEffectsMode();
      break;
    case DECK_CONTROLS:
      drawDeckControlsMode();
      break;
    case NEEDLE_SEARCH:
      drawNeedleSearchMode();
      break;
    case STEMS:
      drawStemsMode();
      break;
    case RB_VIEW:
      drawRbViewMode();
      break;
    case SETUP:
      initializeSetupMode(); // always return to Setup Home
      drawSetupMode();
      break;
  }
  updateStatus();
}

void exitToMenu() {
  currentMode = MENU;
  stopAllModes();
  delay(50);
  drawMenu();
  updateStatus();
}
