#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <Preferences.h>
#include "common_definitions.h"

#define THEME_NVS_NAMESPACE "theme"
#define THEME_NVS_DARK_KEY  "dark"

// Runtime theme state - THEME_* colors declared `extern` in
// common_definitions.h so every screen file can use them unchanged; the
// actual storage and palette values live here.
bool darkMode = true;

uint16_t THEME_BG;
uint16_t THEME_SURFACE;
uint16_t THEME_PRIMARY;
uint16_t THEME_SECONDARY;
uint16_t THEME_ACCENT;
uint16_t THEME_SUCCESS;
uint16_t THEME_WARNING;
uint16_t THEME_ERROR;
uint16_t THEME_TEXT;
uint16_t THEME_TEXT_DIM;

// Function declarations
void initThemeManager();
void applyTheme(bool dark);
void toggleTheme();

// Dark mode: dim DJ-booth palette, vivid neon accents pop against a near-
// black background - the original/default look.
// Light mode: warm-paper background with deepened accent hues (rather than
// simply inverted neon) so contrast holds up under bright venue lighting or
// direct sun, without ever using pure white/black.
void applyTheme(bool dark) {
  darkMode = dark;

  if (dark) {
    THEME_BG        = 0x0841;
    THEME_SURFACE   = 0x2945;
    THEME_PRIMARY   = 0x06FF;
    THEME_SECONDARY = 0xFD20;
    THEME_ACCENT    = 0x07FF;
    THEME_SUCCESS   = 0x07E0;
    THEME_WARNING   = 0xFFE0;
    THEME_ERROR     = 0xF800;
    THEME_TEXT      = 0xFFFF;
    THEME_TEXT_DIM  = 0x8410;
  } else {
    THEME_BG        = 0xEF5C;
    THEME_SURFACE   = 0xF7DE;
    THEME_PRIMARY   = 0x03B1;
    THEME_SECONDARY = 0xC321;
    THEME_ACCENT    = 0x0436;
    THEME_SUCCESS   = 0x1C07;
    THEME_WARNING   = 0xA3A0;
    THEME_ERROR     = 0xB904;
    THEME_TEXT      = 0x2924;
    THEME_TEXT_DIM  = 0x7B8D;
  }
}

// Loads the saved preference (defaults to dark) and applies it. Call once
// during setup(), before the first screen is drawn.
void initThemeManager() {
  Preferences prefs;
  prefs.begin(THEME_NVS_NAMESPACE, true);
  bool saved = prefs.getBool(THEME_NVS_DARK_KEY, true);
  prefs.end();
  applyTheme(saved);
}

// Flips the theme and persists the choice. Caller is responsible for
// redrawing the current screen afterward.
void toggleTheme() {
  applyTheme(!darkMode);

  Preferences prefs;
  prefs.begin(THEME_NVS_NAMESPACE, false);
  prefs.putBool(THEME_NVS_DARK_KEY, darkMode);
  prefs.end();
}

#endif
