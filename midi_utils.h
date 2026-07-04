#ifndef MIDI_UTILS_H
#define MIDI_UTILS_H

#include "common_definitions.h"

// MIDI utility functions
void sendMIDI(byte cmd, byte note, byte vel) {
  if (!deviceConnected) return;

  midiPacket[2] = cmd;
  midiPacket[3] = note;
  midiPacket[4] = vel;
  pCharacteristic->setValue(midiPacket, 5);
  pCharacteristic->notify();
}

// Sends a single Note On to trigger a Rekordbox toggle function.
// Reason: Rekordbox flips its internal state on each Note On received -
// Note Off is irrelevant for toggle buttons (Quantize, Slip, Master Tempo,
// FX On/Off, stem mutes, etc.). Omitting Note Off also ensures MIDI Learn
// always captures the correct message (9014 not 8014).
void sendToggleNote(byte note) {
  sendMIDI(0x90, note, 127);
}

void stopAllModes() {
  for (int i = 0; i < 128; i++) {
    sendMIDI(0x80, i, 0);
  }
}

#endif
