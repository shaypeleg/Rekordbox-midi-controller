#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <Preferences.h>
#include "common_definitions.h"

#define WIFI_NVS_NAMESPACE "wifi"
#define WIFI_NVS_SSID_KEY  "ssid"
#define WIFI_NVS_PASS_KEY  "pass"
#define WIFI_MAX_SCAN_RESULTS 5
#define WIFI_CONNECT_TIMEOUT_MS 5000

struct WiFiNetwork {
  String ssid;
  int32_t rssi;
};

// Function declarations
void initWiFiManager();
void updateWiFiManager();
int scanNetworks(WiFiNetwork results[WIFI_MAX_SCAN_RESULTS]);
bool connectToNetwork(String ssid, String password);
void forgetNetwork();

// Loads any saved credentials and starts a non-blocking connection attempt.
// Actual success/failure is reflected later via updateWiFiManager().
void initWiFiManager() {
  WiFi.mode(WIFI_STA);

  Preferences prefs;
  prefs.begin(WIFI_NVS_NAMESPACE, true);
  String savedSSID = prefs.getString(WIFI_NVS_SSID_KEY, "");
  String savedPass = prefs.getString(WIFI_NVS_PASS_KEY, "");
  prefs.end();

  if (savedSSID.length() > 0) {
    currentSSID = savedSSID;
    wifiConnecting = true;
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  }
}

// Call every loop() iteration - WiFi.begin() is non-blocking, so we poll
// WiFi.status() instead of relying on event callbacks.
void updateWiFiManager() {
  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  if (nowConnected == wifiConnected) return;

  wifiConnected = nowConnected;
  if (wifiConnected) {
    wifiConnecting = false;
    currentSSID = WiFi.SSID();
  }
}

// Scans nearby networks and returns up to WIFI_MAX_SCAN_RESULTS, sorted by
// signal strength (strongest first). Blocks for a couple of seconds.
//
// ESP32 often returns 0 on the first scan while a WiFi.begin() from boot is
// still in progress. Abort that attempt, clear any stale scan, and retry once.
int scanNetworks(WiFiNetwork results[WIFI_MAX_SCAN_RESULTS]) {
  if (wifiConnecting) {
    wifiConnecting = false;
    WiFi.disconnect();
    delay(100);
  }

  WiFi.scanDelete();
  int found = WiFi.scanNetworks();
  if (found <= 0) {
    // First scan after boot/connect often fails; give the radio a beat and retry.
    delay(100);
    found = WiFi.scanNetworks();
  }
  if (found < 0) found = 0;  // WIFI_SCAN_FAILED / WIFI_SCAN_RUNNING

  int total = min(found, 32);
  int order[32];
  for (int i = 0; i < total; i++) order[i] = i;
  for (int i = 0; i < total - 1; i++) {
    for (int j = i + 1; j < total; j++) {
      if (WiFi.RSSI(order[j]) > WiFi.RSSI(order[i])) {
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }

  int count = min(total, WIFI_MAX_SCAN_RESULTS);
  for (int i = 0; i < count; i++) {
    results[i].ssid = WiFi.SSID(order[i]);
    results[i].rssi = WiFi.RSSI(order[i]);
  }

  WiFi.scanDelete();
  return count;
}

// Blocks briefly (up to WIFI_CONNECT_TIMEOUT_MS) waiting for the connection
// so the UI can show an immediate result. Persists credentials on success.
bool connectToNetwork(String ssid, String password) {
  wifiConnecting = true;
  wifiConnected = false;
  currentSSID = ssid;

  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  wifiConnecting = false;
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected) {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, false);
    prefs.putString(WIFI_NVS_SSID_KEY, ssid);
    prefs.putString(WIFI_NVS_PASS_KEY, password);
    prefs.end();
  }

  return wifiConnected;
}

void forgetNetwork() {
  Preferences prefs;
  prefs.begin(WIFI_NVS_NAMESPACE, false);
  prefs.clear();
  prefs.end();

  WiFi.disconnect();
  wifiConnected = false;
  wifiConnecting = false;
  currentSSID = "";
}

#endif
