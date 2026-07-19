"""
DDJ-REV5 MIDI diagnostic

Listens to the DDJ-REV5 CoreMIDI input and prints a human-readable line
for each message (buttons, knobs, faders). Enabled with `./run.sh -d`.

macOS allows multiple listeners on the same MIDI port, so this can run
alongside Rekordbox and the crossfader relay without interfering.
"""

from __future__ import annotations

import logging
import sys
import time
from typing import Optional

import rtmidi

log = logging.getLogger("midi_diag")

DDJ_PORT_NAME_HINT = "DDJ-REV5"

# Known DDJ-REV5 Control Change labels, keyed by (channel_0_indexed, cc).
# Sourced from Rekordbox's DDJ-REV5 MIDI Learn MIXER tab + live capture
# (CrossFader is 14-bit: MSB CC31 + LSB CC63 on channel 7).
KNOWN_CC: dict[tuple[int, int], str] = {
    (6, 31): "CrossFader",
    (6, 63): "CrossFader LSB",
    (6, 11): "CrossFaderCurve",
    (0, 19): "ChannelFader Deck1",
    (1, 19): "ChannelFader Deck2",
    (2, 19): "ChannelFader Deck3",
    (3, 19): "ChannelFader Deck4",
    (0, 7): "EQ High Deck1",
    (1, 7): "EQ High Deck2",
    (2, 7): "EQ High Deck3",
    (3, 7): "EQ High Deck4",
    (0, 11): "EQ Mid Deck1",
    (1, 11): "EQ Mid Deck2",
    (2, 11): "EQ Mid Deck3",
    (3, 11): "EQ Mid Deck4",
    (0, 15): "EQ Low Deck1",
    (1, 15): "EQ Low Deck2",
    (2, 15): "EQ Low Deck3",
    (3, 15): "EQ Low Deck4",
    (0, 4): "Gain Deck1",
    (1, 4): "Gain Deck2",
    (2, 4): "Gain Deck3",
    (3, 4): "Gain Deck4",
}

# Minimum seconds between identical CC lines (faders spam while moving).
CC_PRINT_INTERVAL = 0.05


def _find_port(port_names: list[str], hint: str) -> Optional[int]:
    for i, name in enumerate(port_names):
        if hint.lower() in name.lower():
            return i
    return None


def _rb_code(message: list[int]) -> str:
    """Rekordbox MIDI Learn style: status + data1 as 4 hex digits (e.g. B61F, 9007)."""
    if len(message) >= 2:
        return f"{message[0]:02X}{message[1]:02X}"
    if message:
        return f"{message[0]:02X}"
    return "????"


def _format_message(message: list[int], last_xfader_msb: list[int]) -> Optional[str]:
    """Return a display line, or None to skip (e.g. CrossFader LSB after MSB)."""
    if not message:
        return None

    status = message[0]
    msg_type = status & 0xF0
    code = _rb_code(message)

    if msg_type == 0x90 and len(message) >= 3:
        vel = message[2]
        if vel == 0:
            return f"{code}  Note Off  (vel 0)"
        return f"{code}  Note On   vel {vel:3d}"

    if msg_type == 0x80 and len(message) >= 3:
        return f"{code}  Note Off  vel {message[2]:3d}"

    if msg_type == 0xB0 and len(message) >= 3:
        cc, value = message[1], message[2]
        ch0 = status & 0x0F
        label = KNOWN_CC.get((ch0, cc), "CC")

        # Fold CrossFader LSB into the MSB line as a 14-bit value.
        if ch0 == 6 and cc == 31:
            last_xfader_msb[0] = value
            return f"{code}  {label:22s}  {value:3d}/127"
        if ch0 == 6 and cc == 63:
            msb = last_xfader_msb[0]
            if msb >= 0:
                full = (msb << 7) | value
                # Show the MSB Learn code (B61F), not the LSB code — matches Rekordbox.
                return f"B61F  {'CrossFader':22s}  {msb:3d}/127  14-bit={full:5d}"
            return None

        return f"{code}  {label:22s}  {value:3d}/127"

    if msg_type == 0xE0 and len(message) >= 3:
        value = message[1] | (message[2] << 7)
        return f"{code}  PitchBend  {value}"

    return f"{code}  {_rb_code(message)}  raw={message}"


class MidiDiagnostic:
    """Prints DDJ-REV5 MIDI activity to stdout while the companion app runs."""

    def __init__(self):
        self._midi_in: Optional[rtmidi.MidiIn] = None
        self._last_xfader_msb = [-1]
        self._last_cc_key: Optional[tuple] = None
        self._last_cc_time = 0.0

    def start(self) -> bool:
        midi_in = rtmidi.MidiIn()
        names = midi_in.get_ports()
        port = _find_port(names, DDJ_PORT_NAME_HINT)
        if port is None:
            log.warning(
                f"MIDI diagnostic: no input matching '{DDJ_PORT_NAME_HINT}'. "
                "Is the DDJ-REV5 connected?"
            )
            return False

        midi_in.open_port(port)
        midi_in.ignore_types(sysex=True, timing=True, active_sense=True)
        midi_in.set_callback(self._on_message)
        self._midi_in = midi_in

        print(f"\n=== DDJ-REV5 MIDI diagnostic ===", flush=True)
        print(f"Listening on '{names[port]}'  (Ctrl+C to stop server)\n", flush=True)
        log.info(f"MIDI diagnostic active on '{names[port]}'")
        return True

    def _on_message(self, event, data=None):
        message, _deltatime = event
        line = _format_message(message, self._last_xfader_msb)
        if line is None:
            return

        # Throttle identical CC spam while a fader is held/moving slowly.
        status = message[0] if message else 0
        if (status & 0xF0) == 0xB0 and len(message) >= 3:
            key = (message[0], message[1], message[2])
            now = time.monotonic()
            if key == self._last_cc_key and (now - self._last_cc_time) < CC_PRINT_INTERVAL:
                return
            self._last_cc_key = key
            self._last_cc_time = now

        print(line, flush=True)

    def stop(self):
        if self._midi_in:
            self._midi_in.close_port()
            self._midi_in = None
            print("\n=== MIDI diagnostic stopped ===\n", flush=True)
