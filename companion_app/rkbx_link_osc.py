"""
rkbx_link OSC receiver — live playhead / transport from Rekordbox memory.

Listens for OSC from https://github.com/grufkork/rkbx_link (default destination
127.0.0.1:4460) and exposes per-deck position, BPM, and track title/artist so
the companion server can push a live needle to the CYD.

Degrades gracefully if python-osc is missing or rkbx_link is not running.
"""

from __future__ import annotations

import asyncio
import logging
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

log = logging.getLogger("rkbx_osc")

OSC_LISTEN_HOST = "127.0.0.1"
OSC_LISTEN_PORT = 4460  # rkbx_link default osc.destination port
# Reason: Mac 7.2.16 /N/time can gap 1–2s+; keep last sample usable a bit longer
STALE_AFTER_S = 4.0
# Bridge sparse OSC while playing. Hold at raw+MAX (plateau) — do NOT snap
# back to raw (that caused advance→rewind sawtooth every ~0.7s).
MAX_BRIDGE_S = 2.0


@dataclass
class DeckTransport:
    """Live transport snapshot for one Rekordbox deck."""

    position_s: float = 0.0
    bpm: float = 0.0
    title: str = ""
    artist: str = ""
    updated_at: float = 0.0
    # Reason: rkbx may re-send the same /N/time for 1–2s; track real advances.
    # None = never set (don't use <=0 — monotonic can be near 0 at process start).
    position_changed_at: Optional[float] = None
    last_delta_s: float = 0.0  # last raw move; <0 means scratch/seek back

    @property
    def position_ms(self) -> int:
        return max(0, int(self.position_s * 1000.0))

    def extrapolated_position_s(self, now: Optional[float] = None) -> float:
        """
        Advance position by wall time since the playhead last *changed*.

        rkbx_link on Mac often holds a flat /N/time for ~1–2s then jumps.
        Bridge those gaps, then plateau at raw+MAX_BRIDGE (no snap-back).
        Scratch-back disables forward bridging.
        """
        if self.position_changed_at is None:
            return self.position_s
        t = time.monotonic() if now is None else now
        changed_age = max(0.0, t - self.position_changed_at)
        osc_age = (
            max(0.0, t - self.updated_at) if self.updated_at > 0 else 999.0
        )
        # Scratch / seek back — follow raw only
        if self.last_delta_s < -0.02:
            return self.position_s
        # Fresh OSC repeats of the same time → truly paused at raw
        if changed_age > 0.25 and osc_age < 0.20:
            return self.position_s
        # Plateau (smooth); snap-back to raw was the sawtooth in logs
        if changed_age > MAX_BRIDGE_S:
            return self.position_s + MAX_BRIDGE_S
        return self.position_s + changed_age

    def is_fresh(self, now: Optional[float] = None) -> bool:
        if self.updated_at <= 0:
            return False
        t = time.monotonic() if now is None else now
        return (t - self.updated_at) <= STALE_AFTER_S


@dataclass
class RkbxLinkState:
    """Aggregated transport state from rkbx_link OSC."""

    decks: dict[int, DeckTransport] = field(default_factory=dict)
    master: DeckTransport = field(default_factory=DeckTransport)

    def deck(self, n: int) -> DeckTransport:
        if n not in self.decks:
            self.decks[n] = DeckTransport()
        return self.decks[n]


def normalize_title(title: str) -> str:
    """Normalize a title for fuzzy deck matching."""
    return " ".join((title or "").casefold().split())


def match_deck_index(
    slot_title: str,
    rkbx_state: RkbxLinkState,
    fallback_index: int,
) -> Optional[int]:
    """
    Map a companion slot title to an rkbx_link deck number (1-based).

    Prefers exact normalized title match across decks 1-4. Falls back to
    ``fallback_index`` (0→deck1, 1→deck2) when that deck has fresh data.
    """
    want = normalize_title(slot_title)
    fb = fallback_index + 1  # 0-based slot → 1-based rkbx deck
    if want:
        matches = [
            n
            for n, transport in rkbx_state.decks.items()
            if transport.is_fresh() and normalize_title(transport.title) == want
        ]
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            # Same track on multiple decks — keep slot→deck stable
            if fb in matches:
                return fb
            return matches[0]

    transport = rkbx_state.decks.get(fb)
    if transport is not None and transport.is_fresh():
        return fb
    return None


def build_playhead_decks(
    slot_titles: list[str],
    rkbx_state: RkbxLinkState,
) -> list[dict]:
    """
    Build playhead payload entries for slots A, B, ... from rkbx state.

    Args:
        slot_titles: Title per companion deck slot (index 0 = A).
        rkbx_state: Latest OSC transport state.

    Returns:
        List of dicts with slot, position_ms, and optional bpm.
    """
    now = time.monotonic()
    out: list[dict] = []
    for i, title in enumerate(slot_titles):
        deck_n = match_deck_index(title, rkbx_state, fallback_index=i)
        if deck_n is None:
            continue
        transport = rkbx_state.deck(deck_n)
        if not transport.is_fresh(now):
            continue
        # Bridge rkbx flat-/jump /N/time; CYD hard-syncs each tick (≤100ms).
        pos_s = transport.extrapolated_position_s(now)
        chg_at = transport.position_changed_at
        entry: dict = {
            "slot": chr(65 + i),
            "position_ms": max(0, int(pos_s * 1000.0)),
            "raw_ms": transport.position_ms,
            "osc_age_ms": int(max(0.0, now - transport.updated_at) * 1000.0),
            "changed_age_ms": (
                int(max(0.0, now - chg_at) * 1000.0) if chg_at is not None else 0
            ),
        }
        if transport.bpm > 0:
            entry["bpm"] = round(transport.bpm, 2)
        out.append(entry)
    return out


class RkbxLinkOscReceiver:
    """Async UDP OSC server that ingests rkbx_link transport messages."""

    def __init__(
        self,
        host: str = OSC_LISTEN_HOST,
        port: int = OSC_LISTEN_PORT,
    ):
        self.host = host
        self.port = port
        self.state = RkbxLinkState()
        self.available = False
        self.on_update: Optional[Callable[[], None]] = None
        self._server = None
        self._transport = None

    def _mark(self, transport: DeckTransport) -> None:
        transport.updated_at = time.monotonic()
        if self.on_update is not None:
            self.on_update()

    def _resolve_target(self, address: str) -> Optional[DeckTransport]:
        # /1/time, /master/bpm/current, /2/track/title, ...
        parts = address.strip("/").split("/")
        if not parts:
            return None
        head = parts[0]
        if head == "master":
            return self.state.master
        if head.isdigit():
            n = int(head)
            if 1 <= n <= 4:
                return self.state.deck(n)
        return None

    def _on_time(self, address: str, *args) -> None:
        if not args:
            return
        target = self._resolve_target(address)
        if target is None:
            return
        try:
            new_pos = float(args[0])
        except (TypeError, ValueError):
            return
        now = time.monotonic()
        delta = new_pos - target.position_s
        # ≥5ms move counts as a real advance (not float noise)
        if target.position_changed_at is None or abs(delta) >= 0.005:
            target.last_delta_s = delta
            target.position_changed_at = now
        target.position_s = new_pos
        target.updated_at = now
        if self.on_update is not None:
            self.on_update()

    def _on_bpm(self, address: str, *args) -> None:
        if not args:
            return
        target = self._resolve_target(address)
        if target is None:
            return
        try:
            target.bpm = float(args[0])
        except (TypeError, ValueError):
            return
        self._mark(target)

    def _on_track_field(self, address: str, field_name: str, *args) -> None:
        if not args:
            return
        target = self._resolve_target(address)
        if target is None:
            return
        value = str(args[0])
        if field_name == "title":
            target.title = value
        elif field_name == "artist":
            target.artist = value
        self._mark(target)

    async def start(self) -> bool:
        """Bind the OSC UDP port. Returns False if unavailable."""
        try:
            from pythonosc.dispatcher import Dispatcher
            from pythonosc.osc_server import AsyncIOOSCUDPServer
        except ImportError:
            log.warning(
                "python-osc not installed - live playhead disabled "
                "(pip install python-osc)"
            )
            return False

        dispatcher = Dispatcher()
        dispatcher.map("/*/time", self._on_time)
        dispatcher.map("/*/bpm/current", self._on_bpm)
        dispatcher.map(
            "/*/track/title",
            lambda addr, *a: self._on_track_field(addr, "title", *a),
        )
        dispatcher.map(
            "/*/track/artist",
            lambda addr, *a: self._on_track_field(addr, "artist", *a),
        )

        try:
            self._server = AsyncIOOSCUDPServer(
                (self.host, self.port), dispatcher, asyncio.get_running_loop()
            )
            self._transport, _protocol = await self._server.create_serve_endpoint()
        except OSError as e:
            log.warning(f"rkbx_link OSC bind failed on {self.host}:{self.port}: {e}")
            return False

        self.available = True
        log.info(
            f"Listening for rkbx_link OSC on udp://{self.host}:{self.port} "
            "(enable osc.msg.n.time in rkbx_link config)"
        )
        return True

    def stop(self) -> None:
        if self._transport is not None:
            try:
                self._transport.close()
            except Exception:
                pass
            self._transport = None
        self._server = None
        self.available = False


"""
=== FILE FLOW DOCUMENTATION ===

Functionality: Receive live Rekordbox transport (playhead, BPM, titles) from
rkbx_link over OSC for the CYD companion server.

Flow:
1. Bind UDP 127.0.0.1:4460 (rkbx_link default destination)
2. Map /N/time, /N/bpm/current, /N/track/* into DeckTransport
3. Expose match helpers so slot A/B titles map to the correct OSC deck
4. Companion builds lightweight playhead WebSocket messages from this state

Main Entry Point: RkbxLinkOscReceiver.start / build_playhead_decks

Dependencies:
- pythonosc: Async OSC UDP server (optional; degrades if missing)
"""
