#ifndef HOT_CUE_MODE_H
#define HOT_CUE_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// Layout: 2 rows of 4 buttons per deck, stacked vertically.
// Total vertical budget: 240 - CONTENT_Y(48) = 192px for two decks.
#define HC_BTN_W 68
#define HC_BTN_H 34
#define HC_BTN_GAP 3
#define HC_START_X 8
#define HC_LABEL_H 14

#define HC_D1_LABEL_Y CONTENT_Y
#define HC_D1_ROW1_Y (HC_D1_LABEL_Y + HC_LABEL_H + 1)
#define HC_D1_ROW2_Y (HC_D1_ROW1_Y + HC_BTN_H + HC_BTN_GAP)

#define HC_D2_LABEL_Y (HC_D1_ROW2_Y + HC_BTN_H + 4)
#define HC_D2_ROW1_Y (HC_D2_LABEL_Y + HC_LABEL_H + 1)
#define HC_D2_ROW2_Y (HC_D2_ROW1_Y + HC_BTN_H + HC_BTN_GAP)

static const byte hotCueD1Notes[8] = {
  NOTE_HOTCUE_D1_1, NOTE_HOTCUE_D1_2, NOTE_HOTCUE_D1_3, NOTE_HOTCUE_D1_4,
  NOTE_HOTCUE_D1_5, NOTE_HOTCUE_D1_6, NOTE_HOTCUE_D1_7, NOTE_HOTCUE_D1_8
};
static const byte hotCueD2Notes[8] = {
  NOTE_HOTCUE_D2_1, NOTE_HOTCUE_D2_2, NOTE_HOTCUE_D2_3, NOTE_HOTCUE_D2_4,
  NOTE_HOTCUE_D2_5, NOTE_HOTCUE_D2_6, NOTE_HOTCUE_D2_7, NOTE_HOTCUE_D2_8
};

// Hot cue colors matching Rekordbox default pad colors.
// Order: red, orange, yellow, green, cyan, blue, purple, pink.
static const uint16_t hotCueColors[8] = {
  0xF800, 0xFDA0, 0xFFE0, 0x07E0,
  0x07FF, 0x001F, 0x781F, 0xF81F
};

void initializeHotCueMode();
void drawHotCueMode();
void handleHotCueMode();
void drawHotCueDeck(int deck);
void handleHotCueDeck(int deck);

void initializeHotCueMode() {
  // Stateless — hot cues are momentary triggers, no local toggle state.
}

int hotCueLabelY(int deck) { return (deck == 0) ? HC_D1_LABEL_Y : HC_D2_LABEL_Y; }
int hotCueRow1Y(int deck)  { return (deck == 0) ? HC_D1_ROW1_Y  : HC_D2_ROW1_Y; }
int hotCueRow2Y(int deck)  { return (deck == 0) ? HC_D1_ROW2_Y  : HC_D2_ROW2_Y; }

void drawHotCueMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("HOT CUE");

  sendToggleNote(NOTE_HOTCUE_MODE_ENTER);
  delay(20);
  sendToggleNote(NOTE_HOTCUE_VIEW_SWITCH);
  Serial.println("Hot Cue mode entered - sent mode + view signals");

  drawHotCueDeck(0);
  drawHotCueDeck(1);
}

void drawHotCueDeck(int deck) {
  uint16_t deckColor = (deck == 0) ? THEME_PRIMARY : THEME_ACCENT;
  int labelY = hotCueLabelY(deck);
  int row1Y = hotCueRow1Y(deck);
  int row2Y = hotCueRow2Y(deck);

  tft.setTextColor(deckColor, THEME_BG);
  tft.drawString(deck == 0 ? "DECK 1" : "DECK 2",
                 HC_START_X, labelY, 2);

  for (int i = 0; i < 8; i++) {
    int col = i % 4;
    int rowY = (i < 4) ? row1Y : row2Y;
    int x = HC_START_X + col * (HC_BTN_W + HC_BTN_GAP);

    tft.fillRoundRect(x, rowY, HC_BTN_W, HC_BTN_H, 6, hotCueColors[i]);
    tft.drawRoundRect(x, rowY, HC_BTN_W, HC_BTN_H, 6, THEME_TEXT);

    tft.setTextColor(THEME_BG, hotCueColors[i]);
    tft.drawCentreString(String(i + 1), x + HC_BTN_W / 2,
                         rowY + HC_BTN_H / 2 - 8, 2);
  }
}

// Brief visual flash on tap to give tactile feedback.
void flashHotCueButton(int deck, int cue) {
  int col = cue % 4;
  int rowY = (cue < 4) ? hotCueRow1Y(deck) : hotCueRow2Y(deck);
  int x = HC_START_X + col * (HC_BTN_W + HC_BTN_GAP);

  tft.fillRoundRect(x, rowY, HC_BTN_W, HC_BTN_H, 6, THEME_BG);
  tft.drawRoundRect(x, rowY, HC_BTN_W, HC_BTN_H, 6, hotCueColors[cue]);
  tft.setTextColor(hotCueColors[cue], THEME_BG);
  tft.drawCentreString(String(cue + 1), x + HC_BTN_W / 2,
                       rowY + HC_BTN_H / 2 - 8, 2);

  delay(80);

  tft.fillRoundRect(x, rowY, HC_BTN_W, HC_BTN_H, 6, hotCueColors[cue]);
  tft.drawRoundRect(x, rowY, HC_BTN_W, HC_BTN_H, 6, THEME_TEXT);
  tft.setTextColor(THEME_BG, hotCueColors[cue]);
  tft.drawCentreString(String(cue + 1), x + HC_BTN_W / 2,
                       rowY + HC_BTN_H / 2 - 8, 2);
}

void handleHotCueDeck(int deck) {
  const byte *notes = (deck == 0) ? hotCueD1Notes : hotCueD2Notes;
  int row1Y = hotCueRow1Y(deck);
  int row2Y = hotCueRow2Y(deck);

  for (int i = 0; i < 8; i++) {
    int col = i % 4;
    int rowY = (i < 4) ? row1Y : row2Y;
    int x = HC_START_X + col * (HC_BTN_W + HC_BTN_GAP);

    if (isButtonPressed(x, rowY, HC_BTN_W, HC_BTN_H)) {
      sendToggleNote(notes[i]);
      flashHotCueButton(deck, i);
      Serial.printf("Deck %d Hot Cue %d triggered\n", deck + 1, i + 1);
      return;
    }
  }
}

void handleHotCueMode() {
  if (touch.justPressed &&
      isButtonPressed(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    exitToMenu();
    return;
  }

  if (!touch.justPressed) return;

  handleHotCueDeck(0);
  handleHotCueDeck(1);
}

#endif
