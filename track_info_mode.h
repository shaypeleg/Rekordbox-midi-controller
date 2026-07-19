#ifndef TRACK_INFO_MODE_H
#define TRACK_INFO_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

using namespace websockets;

// Layout constants - content starts well below the back chevron (r=13, cy=32)
#define TI_CONTENT_Y     48
#define TI_META_H        16
#define TI_WF_H          42
#define TI_WF_H_EXPANDED 80
#define TI_WF_H_OVERVIEW 18
#define TI_CUE_BOX_W    12
#define TI_CUE_BOX_H    10
#define TI_MAX_CUES       8
#define TI_WS_RECONNECT_MS 3000
#define TI_NEEDLE_COLOR  0xFFE0  // bright yellow — visible on any waveform color
#define TI_NEEDLE_EDGE   TFT_BLACK

// Title scroll animation (ping-pong marquee for long titles)
#define TI_SCROLL_STEP_MS   50
#define TI_SCROLL_PAUSE_MS 1500
#define TI_SCROLL_PX_STEP     2

// Per-deck data
struct TrackDeck {
  String title;
  String artist;
  float bpm;
  String key;
  int duration_s;
  String comment;
  int numCues;
  struct {
    char letter;
    char label[8];
    uint32_t time_ms;
    uint16_t color565;
  } cues[TI_MAX_CUES];
  uint8_t waveform[320][4];  // [r, g, b, h] per pixel column
  bool hasWaveform;
  uint32_t position_ms;  // live playhead from rkbx_link (0 if unknown)
  bool hasPosition;
};

// Module state
static WebsocketsClient tiWsClient;
static bool tiWsConnected = false;
static bool tiServerFound = false;
static String tiServerHost;
static uint16_t tiServerPort = 9100;
static unsigned long tiLastReconnect = 0;
static bool tiDataReceived = false;
static TrackDeck tiDecks[2];

// Which menu entry opened this module: classic TRACK vs LIVE VIEW zoom
enum TiUiStyle { TI_UI_TRACK = 0, TI_UI_LIVE = 1 };
static TiUiStyle tiUiStyle = TI_UI_TRACK;

// View state: -1 = compact (both decks), 0 = deck A expanded, 1 = deck B expanded
static int tiExpandedDeck = -1;

// Live playhead / zoom (updated by lightweight {"type":"playhead"} WS msgs)
static int tiWfY[2] = {0, 0};
static int tiWfH[2] = {0, 0};
static int tiOverviewY[2] = {0, 0};
static int tiPlayheadX[2] = {-1, -1};
static bool tiPlayheadDirty = false;
static uint8_t tiZoomWindow[2][320][4];  // scrolling zoom strip from companion
static bool tiHasZoomWindow[2] = {false, false};
static int tiElapsedLabelY = 0;

// Swap button: lets user correct A/B deck assignment when detection is wrong
#define TI_SWAP_BTN_X  288
#define TI_SWAP_BTN_Y    8
#define TI_SWAP_BTN_W   28
#define TI_SWAP_BTN_H   28
static bool tiDecksSwapped = false;

// Touch zones for deck tap detection (set during draw)
static int tiDeckTouchY[2] = {0, 0};
static int tiDeckTouchH[2] = {0, 0};

// Hot cue legend state
static int tiExpandedCue = -1;       // which cue label is tap-expanded (-1 = none)
static int tiExpandedCueDeck = -1;   // which deck the expanded cue belongs to
static int tiLegendY[2] = {0, 0};   // Y position of legend row per deck
static int tiLegendH = 14;          // height of legend pills
static int tiPillX[2][TI_MAX_CUES]; // x positions of each pill per deck
static int tiPillW[2][TI_MAX_CUES]; // widths of each pill per deck

// Per-deck title scroll state
struct TitleScroll {
  int offset;        // current pixel offset (0 = start)
  int maxOffset;     // max scroll distance (textWidth - availableWidth)
  int direction;     // +1 scrolling left (showing more), -1 scrolling right (returning)
  unsigned long lastStep;
  bool paused;
  unsigned long pauseStart;
  int drawX;         // x position where title is drawn
  int drawY;         // y position where title is drawn
  int clipW;         // available width for the title
  int font;          // font used for drawing
};
static TitleScroll tiTitleScroll[2] = {{0,0,1,0,false,0,0,0,0,0}, {0,0,1,0,false,0,0,0,0,0}};
static TitleScroll tiCommentScroll[2] = {{0,0,1,0,false,0,0,0,0,0}, {0,0,1,0,false,0,0,0,0,0}};

static uint16_t rgb3to565(uint8_t r3, uint8_t g3, uint8_t b3) {
  uint8_t r = (r3 * 255) / 7;
  uint8_t g = (g3 * 255) / 7;
  uint8_t b = (b3 * 255) / 7;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static uint16_t hexToColor565(const char* hex) {
  if (!hex || hex[0] != '#' || strlen(hex) < 7) return TFT_WHITE;
  long val = strtol(hex + 1, NULL, 16);
  uint8_t r = (val >> 16) & 0xFF;
  uint8_t g = (val >> 8) & 0xFF;
  uint8_t b = val & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Get the display text for a cue pill: label if available, else formatted time
static String tiCueDisplayText(TrackDeck& d, int cueIdx) {
  if (d.cues[cueIdx].label[0] != '\0') {
    return String(d.cues[cueIdx].label);
  }
  int totalSec = d.cues[cueIdx].time_ms / 1000;
  int m = totalSec / 60;
  int s = totalSec % 60;
  return String(m) + ":" + (s < 10 ? "0" : "") + String(s);
}

// Draw the hot cue legend row with variable-width pills.
// Handles overflow by reducing gaps/padding, then truncating labels.
static void tiDrawCueLegend(TrackDeck& d, int deckIdx, int y, int font) {
  if (d.numCues == 0) return;

  tiLegendY[deckIdx] = y;
  int boxH = (font == 2) ? 14 : 12;
  tiLegendH = boxH;
  int maxW = 316;

  // Build display strings for each cue
  String texts[TI_MAX_CUES];
  int textWidths[TI_MAX_CUES];
  for (int i = 0; i < d.numCues; i++) {
    if (i == tiExpandedCue && deckIdx == tiExpandedCueDeck) {
      if (d.cues[i].label[0] != '\0') {
        texts[i] = String(d.cues[i].label);
      } else {
        texts[i] = tiCueDisplayText(d, i);
      }
    } else {
      texts[i] = tiCueDisplayText(d, i);
    }
    textWidths[i] = tft.textWidth(texts[i], font);
  }

  // Calculate total width with default padding (8px per pill) and gap (3px)
  int padding = 8;
  int gap = 3;
  int totalW = 0;
  for (int i = 0; i < d.numCues; i++) totalW += textWidths[i] + padding;
  totalW += (d.numCues - 1) * gap;

  // First pass: reduce gap and padding if overflowing
  if (totalW > maxW) {
    gap = 2;
    padding = 4;
    totalW = 0;
    for (int i = 0; i < d.numCues; i++) totalW += textWidths[i] + padding;
    totalW += (d.numCues - 1) * gap;
  }

  // Second pass: truncate longest labels until fits
  if (totalW > maxW) {
    for (int maxChars = 6; maxChars >= 2 && totalW > maxW; maxChars--) {
      for (int i = 0; i < d.numCues; i++) {
        if (i == tiExpandedCue && deckIdx == tiExpandedCueDeck) continue;
        if ((int)texts[i].length() > maxChars) {
          texts[i] = texts[i].substring(0, maxChars);
          textWidths[i] = tft.textWidth(texts[i], font);
        }
      }
      totalW = 0;
      for (int i = 0; i < d.numCues; i++) totalW += textWidths[i] + padding;
      totalW += (d.numCues - 1) * gap;
    }
  }

  // Draw pills
  int startX = 4;
  int cx = startX;
  for (int i = 0; i < d.numCues; i++) {
    int pw = textWidths[i] + padding;
    tiPillX[deckIdx][i] = cx;
    tiPillW[deckIdx][i] = pw;
    if (cx + pw > 318) break;
    tft.fillRoundRect(cx, y, pw, boxH, 3, d.cues[i].color565);
    tft.setTextColor(TFT_WHITE, d.cues[i].color565);
    int textX = cx + (pw - textWidths[i]) / 2;
    tft.drawString(texts[i], textX, y + 1, font);
    cx += pw + gap;
  }
}

static void tiDiscoverServer() {
  if (!wifiConnected) {
    tiServerFound = false;
    return;
  }
  int n = MDNS.queryService("rekordbox-cyd", "tcp");
  if (n > 0) {
    tiServerHost = MDNS.address(0).toString();
    tiServerPort = MDNS.port(0);
    tiServerFound = true;
  }
}

static void tiConnect() {
  if (!tiServerFound || tiWsConnected) return;
  String url = "ws://" + tiServerHost + ":" + String(tiServerPort);
  tiWsClient.connect(url);
}

static int tiSlotToIndex(const char* slot) {
  if (!slot || !slot[0]) return -1;
  if (slot[0] == 'A' || slot[0] == 'a') return 0;
  if (slot[0] == 'B' || slot[0] == 'b') return 1;
  return -1;
}

static void tiParseDeck(JsonObject deckObj, int idx) {
  TrackDeck& d = tiDecks[idx];
  d.title = deckObj["title"].as<String>();
  d.artist = deckObj["artist"].as<String>();
  d.bpm = deckObj["bpm"] | 0.0f;
  d.key = deckObj["key"].as<String>();
  d.duration_s = deckObj["duration_s"] | 0;
  d.comment = deckObj["comment"].as<String>();

  if (deckObj["position_ms"].isNull()) {
    d.hasPosition = false;
    d.position_ms = 0;
  } else {
    d.position_ms = deckObj["position_ms"] | 0;
    d.hasPosition = true;
  }

  JsonArray cuesArr = deckObj["hot_cues"];
  d.numCues = 0;
  for (JsonObject cue : cuesArr) {
    if (d.numCues >= TI_MAX_CUES) break;
    const char* letter = cue["letter"];
    d.cues[d.numCues].letter = letter ? letter[0] : '?';
    const char* cueComment = cue["comment"];
    if (cueComment && cueComment[0] != '\0') {
      strncpy(d.cues[d.numCues].label, cueComment, 7);
      d.cues[d.numCues].label[7] = '\0';
    } else {
      d.cues[d.numCues].label[0] = '\0';
    }
    d.cues[d.numCues].time_ms = cue["time_ms"] | 0;
    d.cues[d.numCues].color565 = hexToColor565(cue["color"]);
    d.numCues++;
  }

  JsonArray wfArr = deckObj["waveform"];
  if (wfArr.isNull() || wfArr.size() == 0) {
    d.hasWaveform = false;
  } else {
    d.hasWaveform = true;
    int col = 0;
    for (JsonArray pt : wfArr) {
      if (col >= 320) break;
      d.waveform[col][0] = pt[0] | 0;
      d.waveform[col][1] = pt[1] | 0;
      d.waveform[col][2] = pt[2] | 0;
      d.waveform[col][3] = pt[3] | 0;
      col++;
    }
    for (; col < 320; col++) {
      d.waveform[col][0] = 0;
      d.waveform[col][1] = 0;
      d.waveform[col][2] = 0;
      d.waveform[col][3] = 0;
    }
  }
}

static void tiParseWaveWindow(JsonObject deckObj, int idx) {
  JsonArray ww = deckObj["wave_window"];
  if (ww.isNull() || ww.size() == 0) {
    tiHasZoomWindow[idx] = false;
    return;
  }
  tiHasZoomWindow[idx] = true;
  int col = 0;
  for (JsonArray pt : ww) {
    if (col >= 320) break;
    tiZoomWindow[idx][col][0] = pt[0] | 0;
    tiZoomWindow[idx][col][1] = pt[1] | 0;
    tiZoomWindow[idx][col][2] = pt[2] | 0;
    tiZoomWindow[idx][col][3] = pt[3] | 0;
    col++;
  }
  for (; col < 320; col++) {
    tiZoomWindow[idx][col][0] = 0;
    tiZoomWindow[idx][col][1] = 0;
    tiZoomWindow[idx][col][2] = 0;
    tiZoomWindow[idx][col][3] = 0;
  }
}

static void tiParsePlayheadMessage(JsonArray decks) {
  int i = 0;
  for (JsonObject deckObj : decks) {
    int idx = tiSlotToIndex(deckObj["slot"]);
    if (idx < 0) idx = i;  // fallback: array order A, B
    if (idx < 0 || idx >= 2) {
      i++;
      continue;
    }
    tiDecks[idx].position_ms = deckObj["position_ms"] | 0;
    tiDecks[idx].hasPosition = true;
    if (!deckObj["bpm"].isNull()) {
      tiDecks[idx].bpm = deckObj["bpm"] | tiDecks[idx].bpm;
    }
    tiParseWaveWindow(deckObj, idx);
    i++;
  }
  tiPlayheadDirty = true;
}

static void tiOnMessage(WebsocketsMessage msg) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, msg.data());
  if (err) return;

  const char* msgType = doc["type"] | "track";
  JsonArray decks = doc["decks"];

  // Lightweight playhead-only updates — do not rebuild the whole screen
  if (msgType && strcmp(msgType, "playhead") == 0) {
    tiParsePlayheadMessage(decks);
    return;
  }

  int idx = 0;
  for (JsonObject deckObj : decks) {
    if (idx >= 2) break;
    tiParseDeck(deckObj, idx);
    tiPlayheadX[idx] = -1;
    idx++;
  }
  for (; idx < 2; idx++) {
    tiDecks[idx].title = "";
    tiDecks[idx].hasWaveform = false;
    tiDecks[idx].hasPosition = false;
    tiDecks[idx].numCues = 0;
    tiPlayheadX[idx] = -1;
  }
  tiDataReceived = true;
}

static void tiOnEvent(WebsocketsEvent event, String data) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    tiWsConnected = true;
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    tiWsConnected = false;
  }
}

static void tiDrawWaveform(TrackDeck& d, int y, int wfH) {
  if (d.hasWaveform) {
    for (int x = 0; x < 320; x++) {
      uint8_t r3 = d.waveform[x][0];
      uint8_t g3 = d.waveform[x][1];
      uint8_t b3 = d.waveform[x][2];
      uint8_t h = d.waveform[x][3];
      if (h == 0) continue;
      uint16_t color = rgb3to565(r3, g3, b3);
      int barH = (h * wfH) / 31;
      int midY = y + wfH / 2;
      tft.drawFastVLine(x, midY - barH / 2, barH, color);
    }
  } else {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("No waveform", 160, y + wfH / 2 - 4, 1);
  }
}

static void tiDrawCueMarkers(TrackDeck& d, int wfY, int wfH) {
  if (d.duration_s <= 0) return;
  for (int i = 0; i < d.numCues; i++) {
    int xPos = (int)((long)d.cues[i].time_ms * 320 / ((long)d.duration_s * 1000));
    if (xPos < 0) xPos = 0;
    if (xPos > 319 - TI_CUE_BOX_W) xPos = 319 - TI_CUE_BOX_W;
    tft.fillRect(xPos, wfY, TI_CUE_BOX_W, TI_CUE_BOX_H, d.cues[i].color565);
    tft.setTextColor(TFT_WHITE, d.cues[i].color565);
    tft.drawChar(d.cues[i].letter, xPos + 3, wfY + 1, 1);
    tft.drawFastVLine(xPos + TI_CUE_BOX_W / 2, wfY + TI_CUE_BOX_H,
                      wfH - TI_CUE_BOX_H, d.cues[i].color565);
  }
}

// Map position_ms → overview waveform X (0..319)
static int tiPositionToX(TrackDeck& d) {
  if (!d.hasPosition || d.duration_s <= 0) return -1;
  long denom = (long)d.duration_s * 1000L;
  if (denom <= 0) return -1;
  int xPos = (int)((long)d.position_ms * 320L / denom);
  if (xPos < 0) xPos = 0;
  if (xPos > 319) xPos = 319;
  return xPos;
}

// High-contrast needle (yellow core + black edges) — readable on any waveform
static void tiDrawNeedleLine(int x, int y, int h) {
  if (x < 0 || x > 319 || h <= 0) return;
  if (x > 0) tft.drawFastVLine(x - 1, y, h, TI_NEEDLE_EDGE);
  tft.drawFastVLine(x, y, h, TI_NEEDLE_COLOR);
  if (x < 319) tft.drawFastVLine(x + 1, y, h, TI_NEEDLE_EDGE);
}

static void tiRestoreWaveformColumn(TrackDeck& d, int x, int wfY, int wfH) {
  if (x < 0 || x > 319) return;
  for (int dx = -1; dx <= 1; dx++) {
    int cx = x + dx;
    if (cx < 0 || cx > 319) continue;
    tft.drawFastVLine(cx, wfY, wfH, THEME_BG);
    if (d.hasWaveform) {
      uint8_t hh = d.waveform[cx][3];
      if (hh > 0) {
        uint16_t color = rgb3to565(d.waveform[cx][0], d.waveform[cx][1], d.waveform[cx][2]);
        int barH = (hh * wfH) / 31;
        int midY = wfY + wfH / 2;
        tft.drawFastVLine(cx, midY - barH / 2, barH, color);
      }
    }
  }
  if (d.duration_s <= 0) return;
  for (int i = 0; i < d.numCues; i++) {
    int cueX = (int)((long)d.cues[i].time_ms * 320 / ((long)d.duration_s * 1000));
    if (cueX < 0) cueX = 0;
    if (cueX > 319 - TI_CUE_BOX_W) cueX = 319 - TI_CUE_BOX_W;
    int lineX = cueX + TI_CUE_BOX_W / 2;
    for (int dx = -1; dx <= 1; dx++) {
      int cx = x + dx;
      if (cx >= cueX && cx < cueX + TI_CUE_BOX_W) {
        tft.drawFastVLine(cx, wfY, TI_CUE_BOX_H, d.cues[i].color565);
      }
      if (cx == lineX) {
        tft.drawFastVLine(cx, wfY + TI_CUE_BOX_H, wfH - TI_CUE_BOX_H, d.cues[i].color565);
      }
    }
  }
}

static void tiDrawOverviewNeedle(TrackDeck& d, int deckIdx, int wfY, int wfH) {
  tiWfY[deckIdx] = wfY;
  tiWfH[deckIdx] = wfH;
  int xPos = tiPositionToX(d);
  if (xPos < 0) return;
  if (tiPlayheadX[deckIdx] >= 0 && tiPlayheadX[deckIdx] != xPos) {
    tiRestoreWaveformColumn(d, tiPlayheadX[deckIdx], wfY, wfH);
  }
  tiDrawNeedleLine(xPos, wfY, wfH);
  tiPlayheadX[deckIdx] = xPos;
}

// Draw one waveform column from an [r,g,b,h] sample
static void tiDrawSampleColumn(const uint8_t sample[4], int x, int y, int wfH) {
  uint8_t h = sample[3];
  if (h == 0) return;
  uint16_t color = rgb3to565(sample[0], sample[1], sample[2]);
  int barH = (h * wfH) / 31;
  if (barH < 1) barH = 1;
  int midY = y + wfH / 2;
  tft.drawFastVLine(x, midY - barH / 2, barH, color);
}

// Zoomed scrolling waveform: fixed center needle, strip moves under it
static void tiDrawZoomWaveform(int deckIdx, int wfY, int wfH) {
  TrackDeck& d = tiDecks[deckIdx];
  tiWfY[deckIdx] = wfY;
  tiWfH[deckIdx] = wfH;
  tft.fillRect(0, wfY, 320, wfH, THEME_BG);

  if (tiHasZoomWindow[deckIdx]) {
    for (int x = 0; x < 320; x++) {
      tiDrawSampleColumn(tiZoomWindow[deckIdx][x], x, wfY, wfH);
    }
  } else if (d.hasWaveform && d.hasPosition && d.duration_s > 0) {
    // Fallback: synthesize a coarse zoom from the 320 overview strip
    long durMs = (long)d.duration_s * 1000L;
    float bpm = d.bpm > 0 ? d.bpm : 120.0f;
    long windowMs = (long)((8.0f * 60000.0f) / bpm);
    if (windowMs < 500) windowMs = 500;
    if (windowMs > durMs) windowMs = durMs;
    long startMs = (long)d.position_ms - windowMs / 2;
    for (int x = 0; x < 320; x++) {
      long tMs = startMs + ((long)x * windowMs) / 320;
      if (tMs < 0 || tMs >= durMs) continue;
      int src = (int)(tMs * 320L / durMs);
      if (src < 0) src = 0;
      if (src > 319) src = 319;
      tiDrawSampleColumn(d.waveform[src], x, wfY, wfH);
    }
  } else if (d.hasWaveform) {
    tiDrawWaveform(d, wfY, wfH);
  } else {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("No waveform", 160, wfY + wfH / 2 - 4, 1);
  }

  // Fixed center playhead
  if (d.hasPosition) {
    tiDrawNeedleLine(160, wfY, wfH);
  } else {
    tft.setTextColor(THEME_WARNING, THEME_BG);
    tft.drawCentreString("No live playhead", 160, wfY + 2, 1);
  }
}

static void tiDrawOverviewStrip(TrackDeck& d, int deckIdx, int y, int h) {
  tiOverviewY[deckIdx] = y;
  tft.fillRect(0, y, 320, h, THEME_BG);
  if (d.hasWaveform) {
    for (int x = 0; x < 320; x++) {
      tiDrawSampleColumn(d.waveform[x], x, y, h);
    }
  }
  tiDrawCueMarkers(d, y, h);
  int xPos = tiPositionToX(d);
  if (xPos >= 0) tiDrawNeedleLine(xPos, y, h);
}

static String tiFormatDuration(int secs);

static void tiUpdateElapsedLabel(TrackDeck& d) {
  if (tiElapsedLabelY <= 0) return;
  tft.fillRect(200, tiElapsedLabelY, 116, 26, THEME_BG);
  if (!d.hasPosition) return;
  int secs = (int)(d.position_ms / 1000);
  String elapsed = tiFormatDuration(secs);
  tft.setTextColor(TI_NEEDLE_COLOR, THEME_BG);
  int w = tft.textWidth(elapsed, 4);
  tft.drawString(elapsed, 320 - w - 4, tiElapsedLabelY, 4);
}

static void tiUpdatePlayheads() {
  // Classic TRACK screen is static overview + comments — no live needle updates
  if (tiUiStyle != TI_UI_LIVE) {
    tiPlayheadDirty = false;
    return;
  }
  if (!tiPlayheadDirty) return;
  tiPlayheadDirty = false;

  if (tiExpandedDeck == -1) {
    for (int i = 0; i < 2; i++) {
      if (tiWfH[i] <= 0) continue;
      tiDrawOverviewNeedle(tiDecks[i], i, tiWfY[i], tiWfH[i]);
    }
  } else {
    int i = tiExpandedDeck;
    if (tiWfH[i] > 0) {
      tiDrawZoomWaveform(i, tiWfY[i], tiWfH[i]);
      if (tiOverviewY[i] > 0) {
        tiDrawOverviewStrip(tiDecks[i], i, tiOverviewY[i], TI_WF_H_OVERVIEW);
      }
      tiUpdateElapsedLabel(tiDecks[i]);
    }
  }
}

// Draw a title with scrolling if it overflows the available width.
// Sets up scroll state for the given deck so the animation loop can update it.
static void tiDrawTitle(int deckIdx, String& title, int x, int y, int availW, int font) {
  TitleScroll& sc = tiTitleScroll[deckIdx];
  int fullW = tft.textWidth(title, font);

  sc.drawX = x;
  sc.drawY = y;
  sc.clipW = availW;
  sc.font = font;

  if (fullW <= availW) {
    // Title fits - no scrolling needed
    sc.maxOffset = 0;
    sc.offset = 0;
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawString(title, x, y, font);
  } else {
    // Title overflows - draw at current offset (no pre-clear to avoid flicker)
    sc.maxOffset = fullW - availW;
    if (sc.offset > sc.maxOffset) {
      sc.offset = 0;
      sc.direction = 1;
    }
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawString(title, x - sc.offset, y, font);
    // Erase only the spillover strips outside the viewport
    tft.fillRect(0, y, x, tft.fontHeight(font), THEME_BG);
    tft.fillRect(x + availW, y, 320 - (x + availW), tft.fontHeight(font), THEME_BG);
  }
}

// Animate title scroll for one deck. Called from handleTrackInfoMode().
// Returns true if the title region was redrawn (needs no full redraw).
static bool tiUpdateTitleScroll(int deckIdx) {
  TitleScroll& sc = tiTitleScroll[deckIdx];
  if (sc.maxOffset <= 0) return false;

  unsigned long now = millis();

  if (sc.paused) {
    if (now - sc.pauseStart >= TI_SCROLL_PAUSE_MS) {
      sc.paused = false;
      sc.lastStep = now;
    }
    return false;
  }

  if (now - sc.lastStep < TI_SCROLL_STEP_MS) return false;
  sc.lastStep = now;

  sc.offset += sc.direction * TI_SCROLL_PX_STEP;

  if (sc.offset >= sc.maxOffset) {
    sc.offset = sc.maxOffset;
    sc.direction = -1;
    sc.paused = true;
    sc.pauseStart = now;
  } else if (sc.offset <= 0) {
    sc.offset = 0;
    sc.direction = 1;
    sc.paused = true;
    sc.pauseStart = now;
  }

  // Redraw the title at new offset - no fillRect first to avoid flicker.
  // drawString with bg color overwrites old pixels opaquely per character,
  // and since the full text is wider than the clip zone it covers every pixel.
  TrackDeck& d = tiDecks[deckIdx];
  int fontH = tft.fontHeight(sc.font);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(d.title, sc.drawX - sc.offset, sc.drawY, sc.font);
  // Erase only the narrow spillover strips outside the viewport
  tft.fillRect(0, sc.drawY, sc.drawX, fontH, THEME_BG);
  tft.fillRect(sc.drawX + sc.clipW, sc.drawY, 320 - (sc.drawX + sc.clipW), fontH, THEME_BG);

  // Redraw the deck label since left-side cleanup erases it
  bool isA = (tiDecksSwapped ? (deckIdx == 1) : (deckIdx == 0));
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawString(isA ? "A" : "B", 2, sc.drawY, 2);

  return true;
}

// Draw a string with single '>' characters rendered in white, rest in baseColor.
// Returns the total pixel width drawn.
static int tiDrawColoredSegment(const String& text, int x, int y, int font, uint16_t baseColor) {
  int cx = x;
  int i = 0;
  int len = text.length();
  while (i < len) {
    int gt = text.indexOf('>', i);
    if (gt < 0) {
      String tail = text.substring(i);
      tft.setTextColor(baseColor, THEME_BG);
      tft.drawString(tail, cx, y, font);
      cx += tft.textWidth(tail, font);
      break;
    }
    if (gt > i) {
      String seg = text.substring(i, gt);
      tft.setTextColor(baseColor, THEME_BG);
      tft.drawString(seg, cx, y, font);
      cx += tft.textWidth(seg, font);
    }
    tft.setTextColor(TFT_WHITE, THEME_BG);
    tft.drawString(">", cx, y, font);
    cx += tft.textWidth(">", font);
    i = gt + 1;
  }
  return cx - x;
}

// Draw a comment with two-tone coloring split at ">>".
// Text before ">>" = THEME_WARNING (entry transition), after = THEME_ACCENT (exit transition).
// Single '>' characters within each section are rendered in white as punctuation.
static void tiDrawComment(const String& comment, int x, int y, int maxChars, int font) {
  if (comment.length() == 0) return;

  String clipped = comment.substring(0, maxChars);
  int splitIdx = clipped.indexOf(">>");
  if (splitIdx < 0) {
    tiDrawColoredSegment(clipped, x, y, font, THEME_WARNING);
    return;
  }

  String entry = clipped.substring(0, splitIdx);
  String exit = clipped.substring(splitIdx + 2);
  exit.trim();

  int entryW = tiDrawColoredSegment(entry, x, y, font, THEME_WARNING);
  int fontH = tft.fontHeight(font);

  // Filled triangle arrowhead as separator (bright white, vertically centered)
  int triH = fontH - 4;
  int triW = triH / 2 + 2;
  int triX = x + entryW + 4;
  int triCY = y + fontH / 2;
  tft.fillTriangle(triX, triCY - triH / 2,
                   triX, triCY + triH / 2,
                   triX + triW, triCY,
                   TFT_WHITE);

  int sepW = triW + 12;
  if (exit.length() > 0) {
    tiDrawColoredSegment(exit, x + entryW + sepW, y, font, THEME_ACCENT);
  }
}

// Calculate the full pixel width a comment would occupy when rendered.
static int tiCalcCommentWidth(const String& comment, int font) {
  if (comment.length() == 0) return 0;
  int splitIdx = comment.indexOf(">>");
  if (splitIdx < 0) return tft.textWidth(comment, font);

  String entry = comment.substring(0, splitIdx);
  String exit = comment.substring(splitIdx + 2);
  exit.trim();
  int fontH = tft.fontHeight(font);
  int triW = (fontH - 4) / 2 + 2;
  int sepW = triW + 12;
  int exitW = exit.length() > 0 ? tft.textWidth(exit, font) : 0;
  return tft.textWidth(entry, font) + sepW + exitW;
}

// Draw a comment with ping-pong marquee scrolling when it overflows availW.
static void tiDrawCommentWithScroll(int deckIdx, const String& comment,
                                    int x, int y, int availW, int font) {
  if (comment.length() == 0) return;
  TitleScroll& sc = tiCommentScroll[deckIdx];
  int fullW = tiCalcCommentWidth(comment, font);

  sc.drawX = x;
  sc.drawY = y;
  sc.clipW = availW;
  sc.font = font;

  if (fullW <= availW) {
    sc.maxOffset = 0;
    sc.offset = 0;
    tiDrawComment(comment, x, y, 255, font);
  } else {
    sc.maxOffset = fullW - availW;
    if (sc.offset > sc.maxOffset) {
      sc.offset = 0;
      sc.direction = 1;
    }
    tiDrawComment(comment, x - sc.offset, y, 255, font);
    int fontH = tft.fontHeight(font);
    tft.fillRect(0, y, x, fontH, THEME_BG);
    tft.fillRect(x + availW, y, 320 - (x + availW), fontH, THEME_BG);
  }
}

// Animate comment scroll for one deck. Returns true if redrawn.
static bool tiUpdateCommentScroll(int deckIdx) {
  TitleScroll& sc = tiCommentScroll[deckIdx];
  if (sc.maxOffset <= 0) return false;

  unsigned long now = millis();

  if (sc.paused) {
    if (now - sc.pauseStart >= TI_SCROLL_PAUSE_MS) {
      sc.paused = false;
      sc.lastStep = now;
    }
    return false;
  }

  if (now - sc.lastStep < TI_SCROLL_STEP_MS) return false;
  sc.lastStep = now;

  sc.offset += sc.direction * TI_SCROLL_PX_STEP;

  if (sc.offset >= sc.maxOffset) {
    sc.offset = sc.maxOffset;
    sc.direction = -1;
    sc.paused = true;
    sc.pauseStart = now;
  } else if (sc.offset <= 0) {
    sc.offset = 0;
    sc.direction = 1;
    sc.paused = true;
    sc.pauseStart = now;
  }

  TrackDeck& d = tiDecks[deckIdx];
  int fontH = tft.fontHeight(sc.font);
  tft.fillRect(sc.drawX, sc.drawY, sc.clipW, fontH, THEME_BG);
  tiDrawComment(d.comment, sc.drawX - sc.offset, sc.drawY, 255, sc.font);
  tft.fillRect(0, sc.drawY, sc.drawX, fontH, THEME_BG);
  tft.fillRect(sc.drawX + sc.clipW, sc.drawY, 320 - (sc.drawX + sc.clipW), fontH, THEME_BG);

  return true;
}

// Compact deck view: title + waveform + cue legend + comment
static void tiDrawDeckCompact(int deckIdx, int baseY) {
  TrackDeck& d = tiDecks[deckIdx];
  int wfY = baseY + TI_META_H + 5;
  int commentY = wfY + TI_WF_H + 5;

  tiDeckTouchY[deckIdx] = baseY;
  tiDeckTouchH[deckIdx] = commentY + 16 - baseY;

  // BPM + Key on the right (font 2 for readability)
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String meta = String(d.bpm, 0) + " " + d.key;
  int metaW = tft.textWidth(meta, 2);
  int metaX = 320 - metaW - 2;
  tft.drawString(meta, metaX, baseY, 2);

  // Deck label (based on display position, not data index)
  bool isTop = (baseY == TI_CONTENT_Y);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawString(isTop ? "A" : "B", 2, baseY, 2);

  // Title (scrolls if too long)
  int titleX = 16;
  int titleAvailW = metaX - titleX - 6;
  tiDrawTitle(deckIdx, d.title, titleX, baseY, titleAvailW, 2);

  // Overview waveform + cues (+ live needle only in LIVE VIEW)
  tiDrawWaveform(d, wfY, TI_WF_H);
  tiDrawCueMarkers(d, wfY, TI_WF_H);
  if (tiUiStyle == TI_UI_LIVE) {
    tiDrawOverviewNeedle(d, deckIdx, wfY, TI_WF_H);
  }

  // Comment below waveform (scrolls if too long)
  tiDrawCommentWithScroll(deckIdx, d.comment, 4, commentY, 312, 2);
}

// Format seconds into M:SS
static String tiFormatDuration(int secs) {
  int m = secs / 60;
  int s = secs % 60;
  return String(m) + ":" + (s < 10 ? "0" : "") + String(s);
}

// Expanded TRACK: classic tall overview + cue legend + large comment
static void tiDrawDeckExpandedTrack(int deckIdx, int baseY) {
  TrackDeck& d = tiDecks[deckIdx];

  tiDeckTouchY[deckIdx] = baseY;
  tiDeckTouchH[deckIdx] = 240 - baseY;
  tiElapsedLabelY = 0;
  tiOverviewY[deckIdx] = 0;

  bool isA = tiDecksSwapped ? (deckIdx == 1) : (deckIdx == 0);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawString(isA ? "A" : "B", 2, baseY, 2);
  int titleX = 16;
  tiDrawTitle(deckIdx, d.title, titleX, baseY, 320 - titleX - 4, 2);

  int row2Y = baseY + 20;
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawString(String(d.bpm, 1), 4, row2Y, 4);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawString(d.key, 110, row2Y, 4);
  if (d.duration_s > 0) {
    tft.setTextColor(THEME_TEXT, THEME_BG);
    String durStr = tiFormatDuration(d.duration_s);
    int durW = tft.textWidth(durStr, 4);
    tft.drawString(durStr, 320 - durW - 4, row2Y, 4);
  }

  int wfY = row2Y + 30;
  tiWfY[deckIdx] = wfY;
  tiWfH[deckIdx] = TI_WF_H_EXPANDED;
  tiDrawWaveform(d, wfY, TI_WF_H_EXPANDED);
  tiDrawCueMarkers(d, wfY, TI_WF_H_EXPANDED);

  int legendY = wfY + TI_WF_H_EXPANDED + 6;
  tiDrawCueLegend(d, deckIdx, legendY, 2);

  int commentY = legendY + 22;
  if (commentY + 26 <= 240) {
    tiDrawCommentWithScroll(deckIdx, d.comment, 4, commentY, 312, 4);
  }
}

// Expanded LIVE VIEW: zoomed scrolling waveform + overview strip
static void tiDrawDeckExpandedLive(int deckIdx, int baseY) {
  TrackDeck& d = tiDecks[deckIdx];

  tiDeckTouchY[deckIdx] = baseY;
  tiDeckTouchH[deckIdx] = 240 - baseY;

  bool isA = tiDecksSwapped ? (deckIdx == 1) : (deckIdx == 0);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawString(isA ? "A" : "B", 2, baseY, 2);
  int titleX = 16;
  tiDrawTitle(deckIdx, d.title, titleX, baseY, 320 - titleX - 4, 2);

  int row2Y = baseY + 18;
  tiElapsedLabelY = row2Y;
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawString(String(d.bpm, 1), 4, row2Y, 4);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawString(d.key, 100, row2Y, 4);

  if (d.hasPosition) {
    tiUpdateElapsedLabel(d);
  } else if (d.duration_s > 0) {
    tft.setTextColor(THEME_TEXT, THEME_BG);
    String durStr = tiFormatDuration(d.duration_s);
    int durW = tft.textWidth(durStr, 4);
    tft.drawString(durStr, 320 - durW - 4, row2Y, 4);
  }

  int wfY = row2Y + 28;
  tiDrawZoomWaveform(deckIdx, wfY, TI_WF_H_EXPANDED);

  int ovY = wfY + TI_WF_H_EXPANDED + 3;
  tiDrawOverviewStrip(d, deckIdx, ovY, TI_WF_H_OVERVIEW);

  int legendY = ovY + TI_WF_H_OVERVIEW + 4;
  tiDrawCueLegend(d, deckIdx, legendY, 2);

  int commentY = legendY + 18;
  if (commentY + 22 <= 240) {
    tiDrawCommentWithScroll(deckIdx, d.comment, 4, commentY, 312, 2);
  }
}

static void tiDrawDeckExpanded(int deckIdx, int baseY) {
  if (tiUiStyle == TI_UI_LIVE) {
    tiDrawDeckExpandedLive(deckIdx, baseY);
  } else {
    tiDrawDeckExpandedTrack(deckIdx, baseY);
  }
}

static void tiDrawBackButton() {
  drawBackChevron();
}

// Draw the swap arrows icon (↑↓) in the top-right corner
static void tiDrawSwapButton() {
  int cx = TI_SWAP_BTN_X + TI_SWAP_BTN_W / 2;
  int cy = TI_SWAP_BTN_Y + TI_SWAP_BTN_H / 2;
  uint16_t color = tiDecksSwapped ? THEME_WARNING : THEME_TEXT_DIM;

  // Down arrow (left side)
  int ax = cx - 4;
  tft.drawFastVLine(ax, cy - 7, 14, color);
  tft.drawLine(ax - 4, cy + 3, ax, cy + 8, color);
  tft.drawLine(ax + 4, cy + 3, ax, cy + 8, color);

  // Up arrow (right side)
  int bx = cx + 4;
  tft.drawFastVLine(bx, cy - 7, 14, color);
  tft.drawLine(bx - 4, cy - 3, bx, cy - 8, color);
  tft.drawLine(bx + 4, cy - 3, bx, cy - 8, color);
}

static void tiDrawStatus() {
  // Status messages draw in the center of the content area (below back chevron)
  int y = 110;
  int statusH = 20;
  // Clear the status line area to avoid stale text stacking
  tft.fillRect(0, y - 2, 320, statusH + 4, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  if (!wifiConnected) {
    tft.drawCentreString("WiFi not connected", 160, y, 2);
  } else if (!tiServerFound) {
    tft.drawCentreString("Searching for server...", 160, y, 2);
  } else if (!tiWsConnected) {
    tft.drawCentreString("Connecting...", 160, y, 2);
  } else if (!tiDataReceived) {
    tft.drawCentreString("Waiting for track data...", 160, y, 2);
  }
}

static void tiRedrawContent() {
  tft.fillRect(0, TI_CONTENT_Y, 320, 240 - TI_CONTENT_Y, THEME_BG);

  if (!tiDataReceived && !(tiDecks[0].title.length() > 0)) {
    tiDrawStatus();
    return;
  }

  // Apply swap: topIdx is shown at top labeled "A", botIdx at bottom labeled "B"
  int topIdx = tiDecksSwapped ? 1 : 0;
  int botIdx = tiDecksSwapped ? 0 : 1;

  if (tiExpandedDeck == -1) {
    // Compact: both decks stacked
    int topY = TI_CONTENT_Y;
    int botY = TI_CONTENT_Y + TI_META_H + 5 + TI_WF_H + 5 + 16 + 10;
    tiDrawDeckCompact(topIdx, topY);
    tiDrawDeckCompact(botIdx, botY);
  } else {
    // Expanded: single deck fills the screen
    tiDrawDeckExpanded(tiExpandedDeck, TI_CONTENT_Y);
  }
}

// Flag to track whether mDNS has been initialized for this session
static bool tiMdnsReady = false;

static bool tiCallbacksReady = false;

static void tiEnterScreen(TiUiStyle style) {
  tiUiStyle = style;
  tiDataReceived = false;  // force content redraw with current cached decks if any
  tiLastReconnect = 0;
  tiExpandedDeck = -1;
  tiExpandedCue = -1;
  tiExpandedCueDeck = -1;
  tiLegendY[0] = 0;
  tiLegendY[1] = 0;
  tiPlayheadDirty = false;
  tiElapsedLabelY = 0;
  for (int i = 0; i < 2; i++) {
    tiTitleScroll[i] = {0, 0, 1, 0, false, 0, 0, 0, 0, 0};
    tiCommentScroll[i] = {0, 0, 1, 0, false, 0, 0, 0, 0, 0};
    tiWfY[i] = 0;
    tiWfH[i] = 0;
    tiOverviewY[i] = 0;
    tiPlayheadX[i] = -1;
  }
  // Keep WS connection + deck cache across TRACK <-> LIVE VIEW switches
  if (!tiCallbacksReady) {
    tiWsConnected = false;
    tiServerFound = false;
    tiMdnsReady = false;
    memset(tiDecks, 0, sizeof(tiDecks));
    tiHasZoomWindow[0] = false;
    tiHasZoomWindow[1] = false;
    tiWsClient.onMessage(tiOnMessage);
    tiWsClient.onEvent(tiOnEvent);
    tiCallbacksReady = true;
  }
  // If we already have track titles cached, redraw content after draw*()
  if (tiDecks[0].title.length() > 0 || tiDecks[1].title.length() > 0) {
    tiDataReceived = true;
  }
}

void initializeTrackInfoMode() {
  tiEnterScreen(TI_UI_TRACK);
}

void initializeLiveViewMode() {
  tiEnterScreen(TI_UI_LIVE);
}

void drawTrackInfoMode() {
  tft.fillScreen(THEME_BG);
  tiDrawBackButton();
  tiDrawSwapButton();
  tiDrawStatus();
}

void drawLiveViewMode() {
  drawTrackInfoMode();
}

void handleTrackInfoMode() {
  // Back button - ALWAYS checked first, before any blocking operations
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    tiWsClient.close();
    tiWsConnected = false;
    tiExpandedDeck = -1;
    if (tiMdnsReady) {
      MDNS.end();
      tiMdnsReady = false;
    }
    exitToMenu();
    return;
  }

  // Swap button - toggle A/B deck assignment
  if (touch.justPressed &&
      isButtonPressed(TI_SWAP_BTN_X, TI_SWAP_BTN_Y, TI_SWAP_BTN_W, TI_SWAP_BTN_H)) {
    tiDecksSwapped = !tiDecksSwapped;
    // Reset scroll positions since indices are now different
    for (int i = 0; i < 2; i++) {
      tiTitleScroll[i].offset = 0;
      tiTitleScroll[i].direction = 1;
      tiTitleScroll[i].paused = true;
      tiTitleScroll[i].pauseStart = millis();
      tiCommentScroll[i].offset = 0;
      tiCommentScroll[i].direction = 1;
      tiCommentScroll[i].paused = true;
      tiCommentScroll[i].pauseStart = millis();
    }
    // Redraw swap icon (color changes) and content
    tft.fillRect(TI_SWAP_BTN_X, TI_SWAP_BTN_Y, TI_SWAP_BTN_W, TI_SWAP_BTN_H, THEME_BG);
    tiDrawSwapButton();
    tiRedrawContent();
    return;
  }

  // Legend pill tap: expand/collapse a truncated cue label
  if (touch.justPressed && tiDecks[0].title.length() > 0) {
    for (int di = 0; di < 2; di++) {
      int deckIdx = (tiExpandedDeck >= 0) ? tiExpandedDeck : di;
      if (tiLegendY[deckIdx] == 0) continue;
      if (touch.y >= tiLegendY[deckIdx] &&
          touch.y <= tiLegendY[deckIdx] + tiLegendH) {
        TrackDeck& d = tiDecks[deckIdx];
        for (int i = 0; i < d.numCues; i++) {
          if (touch.x >= tiPillX[deckIdx][i] &&
              touch.x <= tiPillX[deckIdx][i] + tiPillW[deckIdx][i]) {
            if (tiExpandedCue == i && tiExpandedCueDeck == deckIdx) {
              tiExpandedCue = -1;
              tiExpandedCueDeck = -1;
            } else {
              tiExpandedCue = i;
              tiExpandedCueDeck = deckIdx;
            }
            tiRedrawContent();
            return;
          }
        }
      }
      if (tiExpandedDeck >= 0) break;
    }
  }

  // Deck tap: toggle between compact/expanded views
  if (touch.justPressed && tiDecks[0].title.length() > 0) {
    if (tiExpandedDeck == -1) {
      for (int i = 0; i < 2; i++) {
        if (isButtonPressed(0, tiDeckTouchY[i], 320, tiDeckTouchH[i])) {
          tiExpandedDeck = i;
          tiExpandedCue = -1;
          tiExpandedCueDeck = -1;
          tiRedrawContent();
          return;
        }
      }
    } else {
      if (touch.y > TI_CONTENT_Y) {
        tiExpandedDeck = -1;
        tiExpandedCue = -1;
        tiExpandedCueDeck = -1;
        tiRedrawContent();
        return;
      }
    }
  }

  // WebSocket polling
  if (tiWsConnected) {
    tiWsClient.poll();
  }

  // Connection/reconnection logic - deferred from draw so back button always works.
  // mDNS init + queryService can each block for 1-2s, but the back button will
  // be checked on the very next loop iteration after the block returns.
  if (!tiWsConnected && wifiConnected) {
    unsigned long now = millis();
    if (now - tiLastReconnect > TI_WS_RECONNECT_MS) {
      tiLastReconnect = now;

      if (!tiMdnsReady) {
        if (!MDNS.begin("cyd-dj")) {
          MDNS.end();
          MDNS.begin("cyd-dj");
        }
        tiMdnsReady = true;
      }

      if (!tiServerFound) {
        tiDiscoverServer();
      }
      if (tiServerFound && !tiWsConnected) {
        tiConnect();
      }
      // Only show status if we don't already have track data displayed
      if (!tiDecks[0].hasWaveform && !tiDecks[1].hasWaveform) {
        tiDrawStatus();
      }
    }
  }

  // Redraw when new track data arrives
  if (tiDataReceived) {
    tiDataReceived = false;
    tiExpandedCue = -1;
    tiExpandedCueDeck = -1;
    for (int i = 0; i < 2; i++) {
      tiTitleScroll[i].offset = 0;
      tiTitleScroll[i].direction = 1;
      tiTitleScroll[i].paused = true;
      tiTitleScroll[i].pauseStart = millis();
      tiCommentScroll[i].offset = 0;
      tiCommentScroll[i].direction = 1;
      tiCommentScroll[i].paused = true;
      tiCommentScroll[i].pauseStart = millis();
    }
    tiRedrawContent();
  }

  // Move playhead needle without a full screen redraw
  tiUpdatePlayheads();

  // Animate title and comment scrolling (runs every loop iteration, self-throttled)
  if (tiExpandedDeck == -1) {
    tiUpdateTitleScroll(0);
    tiUpdateTitleScroll(1);
    tiUpdateCommentScroll(0);
    tiUpdateCommentScroll(1);
  } else {
    tiUpdateTitleScroll(tiExpandedDeck);
    tiUpdateCommentScroll(tiExpandedDeck);
  }
}

void handleLiveViewMode() {
  handleTrackInfoMode();
}

#endif
