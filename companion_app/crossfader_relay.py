"""
Crossfader Relay

Rekordbox does not echo continuous fader/knob positions back to mapped
MIDI devices - its MIDI OUT column only exists for Button-type functions
(LED feedback). Confirmed empty for CrossFader (and every other
Knob/Slider function) on both the DDJ-REV5 and RB-MIDI device profiles in
Preferences > MIDI > MIDI Learn.

So instead of depending on Rekordbox for this, this relay taps the
DDJ-REV5's own crossfader MIDI directly via CoreMIDI. macOS allows
multiple simultaneous listeners on a hardware MIDI input, so this runs
alongside Rekordbox's own connection without interfering with it.

Whenever the crossfader moves, the DDJ-REV5 sends Control Change on
channel 7, CC 31 (`B61F`, per Rekordbox's own MIDI Learn mapping for
CrossFader). This relay forwards a quantized zone value as Control
Change on channel 1, CC 46 (`CC_CROSSFADER_FEEDBACK`) to "RB-MIDI":

  0   = left end  (Deck 2 silenced)
  64  = middle
  127 = right end (Deck 1 silenced)

Only zone *changes* are sent - a fast swipe used to flood BLE with
hundreds of CC updates and contributed to Auto Cue toggle desync
(both Cue channels stuck on). Auto Cue only needs the ends anyway.

Requires:
    pip install python-rtmidi
"""

import logging
from typing import Optional

import rtmidi

log = logging.getLogger("crossfader_relay")

DDJ_PORT_NAME_HINT = "DDJ-REV5"
CYD_PORT_NAME_HINT = "RB-MIDI"

XFADER_STATUS = 0xB6  # Control Change, channel 7 (0-indexed 6) - DDJ-REV5's own CrossFader mapping
XFADER_CC = 0x1F      # 31 - crossfader MSB

FEEDBACK_STATUS = 0xB0  # Control Change, channel 1
FEEDBACK_CC = 46        # CC_CROSSFADER_FEEDBACK in common_definitions.h

# Match the CYD's enter thresholds so the relay and firmware agree.
ZONE_LEFT_MAX = 2
ZONE_RIGHT_MIN = 125


def _find_port(port_names: list[str], hint: str) -> Optional[int]:
    """Returns the index of the first port name containing `hint` (case-insensitive)."""
    for i, name in enumerate(port_names):
        if hint.lower() in name.lower():
            return i
    return None


def _quantize_zone(value: int) -> int:
    if value <= ZONE_LEFT_MAX:
        return 0
    if value >= ZONE_RIGHT_MIN:
        return 127
    return 64


class CrossfaderRelay:
    """Relays the DDJ-REV5's crossfader zone to the CYD over Bluetooth MIDI."""

    def __init__(self):
        self._midi_in: Optional[rtmidi.MidiIn] = None
        self._midi_out: Optional[rtmidi.MidiOut] = None
        self._last_zone: Optional[int] = None

    def start(self) -> bool:
        """Opens both MIDI ports and starts listening. Returns True if active.

        Fails gracefully (logs a warning, returns False) if either device
        isn't currently connected - the rest of the companion app doesn't
        depend on this feature.
        """
        midi_in = rtmidi.MidiIn()
        in_names = midi_in.get_ports()
        in_port = _find_port(in_names, DDJ_PORT_NAME_HINT)
        if in_port is None:
            log.warning(
                f"No MIDI input matching '{DDJ_PORT_NAME_HINT}' - is the "
                "DDJ-REV5 connected? Auto Cue on the CYD won't receive "
                "crossfader feedback until this relay can start."
            )
            return False

        midi_out = rtmidi.MidiOut()
        out_names = midi_out.get_ports()
        out_port = _find_port(out_names, CYD_PORT_NAME_HINT)
        if out_port is None:
            log.warning(
                f"No MIDI output matching '{CYD_PORT_NAME_HINT}' - is the "
                "CYD paired over Bluetooth?"
            )
            return False

        midi_in.open_port(in_port)
        midi_in.ignore_types(sysex=True, timing=True, active_sense=True)
        midi_in.set_callback(self._on_message)

        midi_out.open_port(out_port)

        self._midi_in = midi_in
        self._midi_out = midi_out
        self._last_zone = None
        log.info(
            f"Crossfader relay active: '{in_names[in_port]}' -> '{out_names[out_port]}' "
            "(zone-quantized)"
        )
        return True

    def _on_message(self, event, data=None):
        """rtmidi callback - runs on rtmidi's own thread, not the asyncio loop."""
        message, _deltatime = event
        if len(message) < 3:
            return
        status, cc, value = message[0], message[1], message[2]
        if status != XFADER_STATUS or cc != XFADER_CC:
            return

        zone = _quantize_zone(value)
        if zone == self._last_zone:
            return
        self._last_zone = zone
        self._midi_out.send_message([FEEDBACK_STATUS, FEEDBACK_CC, zone])

    def stop(self):
        if self._midi_in:
            self._midi_in.close_port()
            self._midi_in = None
        if self._midi_out:
            self._midi_out.close_port()
            self._midi_out = None
        self._last_zone = None
