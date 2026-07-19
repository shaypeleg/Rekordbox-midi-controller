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

// --- MIDI receive (Rekordbox -> device) ---
// Everything above this point is output-only. Auto Cue is the first
// feature that needs to read a value back from Rekordbox (crossfader
// position), so this is a minimal receive path: a per-CC "last value"
// cache that mode files can poll. -1 means "no data received yet", so
// callers can tell that apart from a legitimate CC value of 0.
#define MIDI_CC_COUNT 128
int lastReceivedCC[MIDI_CC_COUNT];

void initMIDIRx() {
  for (int i = 0; i < MIDI_CC_COUNT; i++) lastReceivedCC[i] = -1;
}

// Parses an incoming BLE-MIDI packet into MIDI events and caches any
// Control Change values seen. Expected layout is [header][timestamp]
// [status][data...], repeated per event - the same simple layout this
// device uses for its own outgoing packets. Running status (reusing a
// previous status byte without resending it) isn't supported since it
// isn't needed for the single feedback CC this device currently reads.
void handleIncomingMIDIPacket(const uint8_t *data, size_t len) {
  if (len < 1) return;
  size_t i = (data[0] & 0x80) ? 1 : 0;  // skip the BLE-MIDI header byte

  while (i < len) {
    if (!(data[i] & 0x80)) { i++; continue; }  // expect a timestamp byte
    i++;
    if (i >= len || !(data[i] & 0x80)) break;  // expect a status byte next

    uint8_t status = data[i];
    uint8_t type = status & 0xF0;
    i++;

    size_t dataBytes = (type == 0xC0 || type == 0xD0) ? 1 : 2;
    if (i + dataBytes > len) break;

    if (type == 0xB0) {  // Control Change
      uint8_t cc = data[i];
      uint8_t value = data[i + 1];
      if (cc < MIDI_CC_COUNT) lastReceivedCC[cc] = value;
    }

    i += dataBytes;
  }
}

#endif
