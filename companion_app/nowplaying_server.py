"""
Rekordbox Now-Playing Companion Server

Monitors Rekordbox via lsof to detect loaded tracks, extracts metadata,
waveform, and hot cues from pyrekordbox, then pushes updates to connected
CYD (ESP32) clients over WebSocket.

Also starts the crossfader relay (see crossfader_relay.py), which feeds
the DDJ-REV5's crossfader position to the CYD for the Auto Cue feature -
optional, degrades gracefully if python-rtmidi isn't installed or either
MIDI device isn't connected.

Live playhead (optional): listens for rkbx_link OSC on UDP 4460 and pushes
lightweight {"type":"playhead",...} messages at ~25 Hz so the CYD can draw
a moving needle on the static ANLZ waveform.

Auto-discoverable via mDNS (_rekordbox-cyd._tcp.local on port 9100).

Usage:
    ./run.sh              # normal: Track Info server + crossfader relay
    ./run.sh -d           # also print live DDJ-REV5 MIDI to the terminal
    ./run.sh -h           # help

Requires:
    pip install pyrekordbox websockets zeroconf python-rtmidi python-osc
"""

import argparse
import asyncio
import base64
import json
import logging
import os
import signal
import socket
import subprocess
import sys
import time
from typing import Optional

import websockets
from zeroconf import ServiceInfo

try:
    from crossfader_relay import CrossfaderRelay
except ImportError:
    CrossfaderRelay = None  # python-rtmidi not installed - Auto Cue relay disabled

try:
    from midi_diagnostic import MidiDiagnostic
except ImportError:
    MidiDiagnostic = None

try:
    from rkbx_link_osc import RkbxLinkOscReceiver, build_playhead_decks
except ImportError:
    RkbxLinkOscReceiver = None  # type: ignore
    build_playhead_decks = None  # type: ignore

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] %(levelname)s - %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("nowplaying")
# mDNS advertisement attracts non-WebSocket probes on port 9100; those fail
# the handshake and the websockets library logs them as ERROR by default.
# Quiet that noise - real client connect/disconnect still goes through our logger.
logging.getLogger("websockets.server").setLevel(logging.CRITICAL)

WS_PORT = 9100
POLL_INTERVAL = 1.0
# Reason: playhead is a clock sync only; CYD extrapolates + scrolls locally.
PLAYHEAD_INTERVAL = 0.05  # 20 Hz — plenty with on-device extrapolation
# Dense strip loaded once (chunked). Not embedded in the track JSON.
CYD_WAVE_POINTS = 2048
WAVE_CHUNK_COLS = 256  # ~512 raw bytes / chunk — safe for ESP32 heap
# Set via ./run.sh --playhead-debug or CYD_PLAYHEAD_DEBUG=1
PLAYHEAD_DEBUG = os.environ.get("CYD_PLAYHEAD_DEBUG", "").strip() in (
    "1",
    "true",
    "yes",
)

AUDIO_EXTENSIONS = (
    ".mp3", ".m4a", ".wav", ".flac", ".aiff", ".aif",
    ".ogg", ".wma", ".alac", ".aac",
)

KIND_TO_PAD = {1: 0, 2: 1, 3: 2, 5: 3, 6: 4, 7: 5, 8: 6, 9: 7}
PAD_LETTERS = "ABCDEFGH"
PAD_DEFAULT_COLORS = [
    "#e01030",  # A - Red
    "#00b4ff",  # B - Aqua/Cyan Blue
    "#10b820",  # C - Green
    "#f08000",  # D - Orange/Amber
    "#10b820",  # E - Green
    "#e06828",  # F - Burnt Orange
    "#9040e0",  # G - Purple/Violet
    "#e040a0",  # H - Pink/Magenta
]

CYD_WIDTH = 320


def get_local_ip() -> str:
    """Get the machine's local network IP address."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def get_all_rekordbox_audio_fds() -> list[tuple[str, str]]:
    """Use lsof to find ALL audio files currently open by Rekordbox.

    Returns list of (fd, path) tuples for every open audio file, including
    cached/buffered tracks that aren't currently on a deck.
    """
    try:
        result = subprocess.run(
            ["lsof", "-c", "rekordbox"],
            capture_output=True, text=True, timeout=10,
        )
        if result.returncode != 0:
            return []

        entries = []
        for line in result.stdout.splitlines():
            lower_line = line.lower()
            if not any(ext in lower_line for ext in AUDIO_EXTENSIONS):
                continue
            parts = line.split(None, 8)
            if len(parts) < 9:
                continue
            fd = parts[3]
            path = parts[8]
            if not path.lower().endswith(AUDIO_EXTENSIONS):
                continue
            low_path = path.lower()
            if "rekordbox.app/" in low_path or "/sampler/" in low_path:
                continue
            if os.path.isfile(path):
                entries.append((fd, path))

        # Deduplicate by (fd, path) pair but allow same path with different fds
        seen = set()
        unique = []
        for fd, path in entries:
            key = (fd, path)
            if key not in seen:
                seen.add(key)
                unique.append((fd, path))

        return unique

    except (subprocess.TimeoutExpired, FileNotFoundError, OSError) as e:
        log.warning(f"lsof error: {e}")
        return []


class DeckTracker:
    """Tracks which 2 audio files are currently on Rekordbox decks.

    Rekordbox keeps previously loaded files cached (open file descriptors),
    so lsof shows more than 2 audio files at any time. This class tracks
    FD transitions over time to determine which files were most recently
    loaded (= currently on deck).

    Deck assignment strategy (in priority order):
    1. If a deck's file DISAPPEARED and a new file appeared in the same poll,
       the new file REPLACED it on that deck slot.
    2. If no disappearance provides a signal, use a timing heuristic:
       quick successive loads → same deck; spaced loads → other deck.
    3. If a deck slot is empty, fill it from remaining open audio files.
    """

    QUICK_LOAD_WINDOW = 5.0  # seconds

    def __init__(self):
        self.deck_paths: list[Optional[str]] = [None, None]
        self.deck_load_time: list[float] = [0.0, 0.0]
        self.last_replaced_deck: int = 1
        self.prev_fd_set: set[str] = set()
        self.prev_path_set: set[str] = set()
        self.prev_path_fd_count: dict[str, int] = {}
        self._initialized = False

    def _pick_deck_to_replace(self, now: float) -> int:
        """Fallback heuristic when no disappearance signal is available."""
        time_since_last = now - max(self.deck_load_time)
        if time_since_last < self.QUICK_LOAD_WINDOW:
            return self.last_replaced_deck
        else:
            return 1 - self.last_replaced_deck

    def update(self, entries: list[tuple[str, str]]) -> tuple[bool, list[Optional[str]]]:
        """Process new lsof entries and return (changed, [deck_a, deck_b]).

        Detects deck loads by watching for new file descriptors appearing
        and old paths disappearing.
        """
        now = time.time()

        current_fd_set = {fd for fd, _ in entries}
        current_paths = {path for _, path in entries}

        # Count FDs per path to detect instant double
        path_fd_count: dict[str, int] = {}
        for _, path in entries:
            path_fd_count[path] = path_fd_count.get(path, 0) + 1

        if not self._initialized:
            self._initialized = True
            self.prev_fd_set = current_fd_set
            self.prev_path_set = current_paths
            self.prev_path_fd_count = path_fd_count

            if entries:
                # Check for instant double at startup (same path, 2+ FDs)
                for path, count in path_fd_count.items():
                    if count >= 2:
                        self.deck_paths = [path, path]
                        self.deck_load_time = [now, now]
                        log.info(f"  Initial: instant double - {os.path.basename(path)}")
                        return (True, self.deck_paths[:])

                # Best guess: prefer paths with MORE FDs open (active playback
                # uses multiple FDs for reading), then by lowest FD number for
                # A/B ordering. Cached tracks typically have only 1 FD.
                path_scores: dict[str, int] = {}
                path_min_fd: dict[str, str] = {}
                for fd, path in entries:
                    path_scores[path] = path_scores.get(path, 0) + 1
                    if path not in path_min_fd or fd < path_min_fd[path]:
                        path_min_fd[path] = fd
                # Sort by FD count descending (most active), then min FD ascending
                ranked = sorted(
                    path_scores.keys(),
                    key=lambda p: (-path_scores[p], path_min_fd.get(p, "")),
                )
                self.deck_paths[0] = ranked[0] if len(ranked) > 0 else None
                self.deck_paths[1] = ranked[1] if len(ranked) > 1 else None
                self.deck_load_time = [now, now]

            log.info(
                f"  Initial deck guess: A={os.path.basename(self.deck_paths[0] or '')} "
                f"B={os.path.basename(self.deck_paths[1] or '')}"
            )
            return (True, self.deck_paths[:])

        # --- Detect disappeared deck tracks (strongest assignment signal) ---
        disappeared_decks: list[int] = []
        for i in range(2):
            if self.deck_paths[i] and self.deck_paths[i] not in current_paths:
                disappeared_decks.append(i)

        # --- Detect new file descriptors ---
        new_fds = current_fd_set - self.prev_fd_set
        new_paths_from_new_fds = []
        for fd, path in entries:
            if fd in new_fds:
                new_paths_from_new_fds.append(path)

        # Deduplicate while preserving order
        seen_new: set[str] = set()
        unique_new_paths = []
        for p in new_paths_from_new_fds:
            if p not in seen_new:
                seen_new.add(p)
                unique_new_paths.append(p)

        changed = False

        # --- PRIORITY 1: Match new tracks to disappeared deck slots ---
        # If a deck's file vanished and a new file appeared, the new file
        # replaced it on that deck.
        unassigned_new_paths = []
        for path in unique_new_paths:
            if path == self.deck_paths[0] or path == self.deck_paths[1]:
                existing_deck = 0 if path == self.deck_paths[0] else 1
                other_deck = 1 - existing_deck
                prev_count = self.prev_path_fd_count.get(path, 0)
                curr_count = path_fd_count.get(path, 0)

                # Instant double: FD count increased AND the other deck has
                # a different track (or is empty)
                if (curr_count > prev_count and
                        self.deck_paths[other_deck] != path):
                    old_name = os.path.basename(self.deck_paths[other_deck] or "")
                    self.deck_paths[other_deck] = path
                    self.deck_load_time[other_deck] = now
                    self.last_replaced_deck = other_deck
                    changed = True
                    log.info(
                        f"  Instant double on Deck {chr(65 + other_deck)}: "
                        f"{os.path.basename(path)}"
                    )
                    # Remove from disappeared if the other deck was flagged
                    if other_deck in disappeared_decks:
                        disappeared_decks.remove(other_deck)
                else:
                    self.deck_load_time[existing_deck] = now
                    self.last_replaced_deck = existing_deck
                    changed = True
                    log.info(f"  Deck {chr(65 + existing_deck)} reload: {os.path.basename(path)}")
            elif disappeared_decks:
                idx = disappeared_decks.pop(0)
                old_name = os.path.basename(self.deck_paths[idx] or "")
                self.deck_paths[idx] = path
                self.deck_load_time[idx] = now
                self.last_replaced_deck = idx
                changed = True
                log.info(
                    f"  Deck {chr(65 + idx)} replaced (file closed): "
                    f"{old_name} -> {os.path.basename(path)}"
                )
            else:
                unassigned_new_paths.append(path)

        # --- PRIORITY 2: Use timing heuristic for remaining new paths ---
        for path in unassigned_new_paths:
            if self.deck_paths[0] is None:
                idx = 0
            elif self.deck_paths[1] is None:
                idx = 1
            else:
                idx = self._pick_deck_to_replace(now)

            old_name = os.path.basename(self.deck_paths[idx] or "")
            self.deck_paths[idx] = path
            self.deck_load_time[idx] = now
            self.last_replaced_deck = idx
            changed = True
            log.info(
                f"  Replacing Deck {chr(65 + idx)}: {old_name} "
                f"-> {os.path.basename(path)}"
            )

        # --- Clear any remaining disappeared decks with no replacement ---
        for i in disappeared_decks:
            log.info(f"  Deck {chr(65+i)} track closed, clearing")
            self.deck_paths[i] = None
            self.deck_load_time[i] = 0.0
            changed = True

        # --- PRIORITY 3: Fill empty deck slots from open audio files ---
        # If a deck is empty but there are untracked open files, assign one.
        # Allow assigning a path already on the other deck if FD count >= 2
        # (instant double that was missed in Priority 1).
        for i in range(2):
            if self.deck_paths[i] is None:
                other = 1 - i
                # First try paths not on the other deck
                for _, path in entries:
                    if path != self.deck_paths[0] and path != self.deck_paths[1]:
                        self.deck_paths[i] = path
                        self.deck_load_time[i] = now
                        changed = True
                        log.info(
                            f"  Filling empty Deck {chr(65+i)}: "
                            f"{os.path.basename(path)}"
                        )
                        break
                # If still empty, allow instant double (same path, 2+ FDs)
                if self.deck_paths[i] is None and self.deck_paths[other]:
                    other_path = self.deck_paths[other]
                    if path_fd_count.get(other_path, 0) >= 2:
                        self.deck_paths[i] = other_path
                        self.deck_load_time[i] = now
                        changed = True
                        log.info(
                            f"  Filling Deck {chr(65+i)} (instant double): "
                            f"{os.path.basename(other_path)}"
                        )

        self.prev_fd_set = current_fd_set
        self.prev_path_set = current_paths
        self.prev_path_fd_count = path_fd_count
        return (changed, self.deck_paths[:])


def extract_waveform_points(anlz_files, num_points: int) -> Optional[list]:
    """Extract waveform data downsampled to ``num_points``.

    Returns list of [r, g, b, h] where r/g/b are 0-7 and h is 0-31.
    """
    if not anlz_files or num_points <= 0:
        return None

    all_tags = []
    for anlz in anlz_files.values():
        all_tags.extend(anlz.tags)

    # Prefer PWV5 (color detail)
    for tag in all_tags:
        if tag.type == "PWV5":
            try:
                heights, colors = tag.get()
                n = len(heights)
                step_size = max(1, n // num_points)
                wf = []
                for i in range(0, min(n, num_points * step_size), step_size):
                    h = int(heights[i] * 31)
                    r, g, b = int(colors[i][0]), int(colors[i][1]), int(colors[i][2])
                    wf.append([r, g, b, h])
                while len(wf) < num_points:
                    wf.append([0, 0, 0, 0])
                return wf[:num_points]
            except Exception:
                continue

    # Fallback: PWV4
    for tag in all_tags:
        if tag.type == "PWV4":
            try:
                heights, colors, _ = tag.get()
                n = len(heights)
                step_size = max(1, n // num_points)
                max_h = max(1, float(heights.max()))
                wf = []
                for i in range(0, min(n, num_points * step_size), step_size):
                    h_val = heights[i]
                    h_norm = int(
                        (h_val[0] if hasattr(h_val, "__len__") else h_val) / max_h * 31
                    )
                    c = colors[i]
                    layer = c[0] if hasattr(c[0], "__len__") else c
                    r = min(7, int(layer[0] / 20))
                    g = min(7, int(layer[1] / 20))
                    b = min(7, int(layer[2] / 20))
                    wf.append([r, g, b, h_norm])
                while len(wf) < num_points:
                    wf.append([0, 0, 0, 0])
                return wf[:num_points]
            except Exception:
                continue

    # Fallback: PWAV mono
    for tag in all_tags:
        if tag.type == "PWAV":
            try:
                result = tag.get()
                heights = result[0] if isinstance(result, tuple) else result
                n = len(heights)
                step_size = max(1, n // num_points)
                max_h = max(1, float(max(heights)))
                wf = []
                for i in range(0, min(n, num_points * step_size), step_size):
                    val = heights[i]
                    if hasattr(val, "__len__"):
                        val = val[0]
                    h_norm = int(float(val) / max_h * 31)
                    wf.append([0, 4, 7, h_norm])
                while len(wf) < num_points:
                    wf.append([0, 0, 0, 0])
                return wf[:num_points]
            except Exception:
                continue

    return None


def extract_waveform_320(anlz_files) -> Optional[list]:
    """Overview waveform for the CYD (full track → 320 columns)."""
    return extract_waveform_points(anlz_files, CYD_WIDTH)


def pack_wave_u16(wf: list) -> bytes:
    """Pack [r,g,b,h] into 2 bytes/col: r3|g3|b3|h5 little-endian uint16."""
    out = bytearray(len(wf) * 2)
    for i, pt in enumerate(wf):
        r = int(pt[0]) & 7
        g = int(pt[1]) & 7
        b = int(pt[2]) & 7
        h = int(pt[3]) & 31
        v = r | (g << 3) | (b << 6) | (h << 9)
        out[i * 2] = v & 0xFF
        out[i * 2 + 1] = (v >> 8) & 0xFF
    return bytes(out)


def extract_hot_cues(content) -> list[dict]:
    """Extract hot cues from the database content object."""
    if not content.Cues:
        return []

    cues = []
    seen = set()
    for cue in content.Cues:
        pad_idx = KIND_TO_PAD.get(cue.Kind)
        if pad_idx is None or pad_idx in seen:
            continue
        seen.add(pad_idx)
        cues.append({
            "letter": PAD_LETTERS[pad_idx],
            "time_ms": cue.InMsec or 0,
            "color": PAD_DEFAULT_COLORS[pad_idx],
            "comment": cue.Comment or "",
        })
    cues.sort(key=lambda c: c["time_ms"])
    return cues


def build_deck_payload(db, filepath: str, slot: str) -> Optional[tuple[dict, Optional[bytes]]]:
    """Build metadata JSON + packed high-res waveform bytes for one deck.

    Returns ``(deck_dict, wave_u16_bytes_or_None)``. Waveform is NOT inside
    the track JSON — it is sent afterward in small ``type:wave`` chunks.
    """
    content = db.get_content().filter_by(FolderPath=filepath).first()
    if content is None:
        return None

    title = content.Title or os.path.basename(filepath)
    artist = "Unknown"
    if hasattr(content, "Artist") and content.Artist:
        artist = content.Artist.Name if hasattr(content.Artist, "Name") else str(content.Artist)

    key = "?"
    if hasattr(content, "Key") and content.Key:
        key = content.Key.ScaleName if hasattr(content.Key, "ScaleName") else str(content.Key)

    bpm = content.BPM / 100.0 if content.BPM else 0.0
    duration_s = content.Length or 0
    comment = content.Commnt or ""

    wave_bytes = None
    try:
        anlz_files = db.read_anlz_files(content)
        wave_hi = extract_waveform_points(anlz_files, CYD_WAVE_POINTS)
        if wave_hi:
            wave_bytes = pack_wave_u16(wave_hi)
    except Exception as e:
        log.warning(f"Waveform extraction failed for {slot}: {e}")

    hot_cues = extract_hot_cues(content)

    deck = {
        "slot": slot,
        "title": title,
        "artist": artist,
        "bpm": round(bpm, 1),
        "key": key,
        "duration_s": duration_s,
        "comment": comment,
        "hot_cues": hot_cues,
    }
    return deck, wave_bytes


class NowPlayingState:
    """Manages the current state and connected WebSocket clients."""

    def __init__(self):
        self.clients: set = set()
        self.tracker = DeckTracker()
        self.current_payload: str = json.dumps({"type": "track", "decks": []})
        self.wave_blobs: dict[str, bytes] = {}  # slot -> packed u16 waveform
        self._db = None
        self.deck_titles: list[str] = []
        self._last_playhead_pos: dict[str, int] = {}
        self._ph_stall: dict[str, int] = {}
        self._ph_paused: dict[str, bool] = {}
        self._ph_log_at: float = 0.0
        self.rkbx: Optional[object] = None
        self.playhead_debug: bool = PLAYHEAD_DEBUG

    def _get_db(self):
        if self._db is None:
            from pyrekordbox import Rekordbox6Database
            self._db = Rekordbox6Database()
        return self._db

    async def register(self, ws):
        self.clients.add(ws)
        log.info(f"Client connected ({len(self.clients)} total)")
        try:
            await ws.send(self.current_payload)
            await self._send_waves_to(ws)
        except websockets.ConnectionClosed:
            pass

    def unregister(self, ws):
        self.clients.discard(ws)
        log.info(f"Client disconnected ({len(self.clients)} total)")

    async def broadcast(self, message: str):
        if not self.clients:
            return
        disconnected = set()
        for ws in self.clients:
            try:
                await ws.send(message)
            except websockets.ConnectionClosed:
                disconnected.add(ws)
        self.clients -= disconnected

    async def _send_waves_to(self, ws) -> None:
        """Send chunked high-res waveforms to one client."""
        for slot, blob in self.wave_blobs.items():
            n = len(blob) // 2
            for off in range(0, n, WAVE_CHUNK_COLS):
                end = min(off + WAVE_CHUNK_COLS, n)
                chunk = blob[off * 2 : end * 2]
                msg = json.dumps({
                    "type": "wave",
                    "slot": slot,
                    "off": off,
                    "n": n,
                    "data": base64.b64encode(chunk).decode("ascii"),
                })
                await ws.send(msg)
                await asyncio.sleep(0.02)

    async def broadcast_waves(self) -> None:
        """Send chunked waveforms to all clients (after a track update)."""
        if not self.clients or not self.wave_blobs:
            return
        for slot, blob in self.wave_blobs.items():
            n = len(blob) // 2
            log.info(f"  Wave {slot}: {n} cols in chunks of {WAVE_CHUNK_COLS}")
            for off in range(0, n, WAVE_CHUNK_COLS):
                end = min(off + WAVE_CHUNK_COLS, n)
                chunk = blob[off * 2 : end * 2]
                msg = json.dumps({
                    "type": "wave",
                    "slot": slot,
                    "off": off,
                    "n": n,
                    "data": base64.b64encode(chunk).decode("ascii"),
                })
                await self.broadcast(msg)
                await asyncio.sleep(0.02)

    def _merge_playhead_into_decks(self, decks: list[dict]) -> None:
        """Attach position_ms from rkbx_link when available (full track msgs)."""
        if self.rkbx is None or build_playhead_decks is None:
            return
        titles = [d.get("title", "") for d in decks]
        for entry in build_playhead_decks(titles, self.rkbx.state):
            for deck in decks:
                if deck.get("slot") == entry["slot"]:
                    deck["position_ms"] = entry["position_ms"]

    def _log_playhead_debug(self, decks: list[dict]) -> None:
        """Throttle playhead traces so they can be lined up with CYD Serial."""
        if not self.playhead_debug:
            return
        now = time.monotonic()
        force = False
        for entry in decks:
            slot = entry["slot"]
            pos = int(entry["position_ms"])
            raw = int(entry.get("raw_ms", pos))
            prev = self._last_playhead_pos.get(slot)
            if prev is None:
                d_in = 0
                stall = 0
            else:
                d_in = pos - prev
                # Any real move (fwd or back) clears stall; flat = possible pause
                if abs(d_in) > 8:
                    stall = 0
                else:
                    stall = self._ph_stall.get(slot, 0) + 1
            self._ph_stall[slot] = stall
            # Flat output = paused or bridge plateau (not the snap-back bug)
            paused = stall >= 3
            was = self._ph_paused.get(slot, False)
            if paused != was:
                force = True
                log.info(
                    "[PH] slot=%s PAUSE→%s at in=%d raw=%d",
                    slot,
                    "YES" if paused else "NO",
                    pos,
                    raw,
                )
            self._ph_paused[slot] = paused
            entry["_dbg_d_in"] = d_in
            entry["_dbg_stall"] = stall
            entry["_dbg_paused"] = int(paused)

        if force or (now - self._ph_log_at) >= 0.2:
            self._ph_log_at = now
            parts = []
            for entry in decks:
                parts.append(
                    f"{entry['slot']}:in={entry['position_ms']}"
                    f":raw={entry.get('raw_ms', '?')}"
                    f":d_in={entry.get('_dbg_d_in', 0):+d}"
                    f":osc_age={entry.get('osc_age_ms', '?')}"
                    f":chg_age={entry.get('changed_age_ms', '?')}"
                    f":paused={entry.get('_dbg_paused', 0)}"
                    f":stall={entry.get('_dbg_stall', 0)}"
                )
            log.info("[PH] %s", " | ".join(parts))

    async def broadcast_playhead(self):
        """Push tiny position-only playhead msgs; CYD scrolls zoom locally."""
        if (
            self.rkbx is None
            or not getattr(self.rkbx, "available", False)
            or build_playhead_decks is None
            or not self.deck_titles
            or not self.clients
        ):
            return

        decks = build_playhead_decks(self.deck_titles, self.rkbx.state)
        if not decks:
            return

        self._log_playhead_debug(decks)
        slim = []
        for entry in decks:
            pos = int(entry["position_ms"])
            self._last_playhead_pos[entry["slot"]] = pos
            slim.append({
                "slot": entry["slot"],
                "position_ms": pos,
                **({"bpm": entry["bpm"]} if "bpm" in entry else {}),
            })
        payload = json.dumps({"type": "playhead", "decks": slim})
        await self.broadcast(payload)

    async def poll_once(self):
        """Check for track changes and broadcast if needed."""
        entries = get_all_rekordbox_audio_fds()
        changed, deck_paths = self.tracker.update(entries)

        if not changed:
            return

        log.info(f"Deck state updated (lsof shows {len(entries)} audio files)")

        active_paths = [p for p in deck_paths if p is not None]
        if not active_paths:
            self.deck_titles = []
            self.wave_blobs = {}
            self._last_playhead_pos = {}
            self.current_payload = json.dumps({"type": "track", "decks": []})
            await self.broadcast(self.current_payload)
            return

        db = self._get_db()
        decks = []
        waves: dict[str, bytes] = {}
        for i, fp in enumerate(active_paths):
            slot = chr(65 + i)
            built = build_deck_payload(db, fp, slot)
            if not built:
                continue
            deck, wave_bytes = built
            decks.append(deck)
            if wave_bytes:
                waves[slot] = wave_bytes
            log.info(f"  Deck {slot}: {deck['title']} - {deck['artist']}")

        self.deck_titles = [d["title"] for d in decks]
        self.wave_blobs = waves
        self._last_playhead_pos = {}
        self._merge_playhead_into_decks(decks)
        self.current_payload = json.dumps({"type": "track", "decks": decks})
        await self.broadcast(self.current_payload)
        await self.broadcast_waves()


state = NowPlayingState()


async def ws_handler(websocket):
    """Handle a single WebSocket client connection."""
    await state.register(websocket)
    try:
        async for _ in websocket:
            pass
    except websockets.ConnectionClosed:
        pass
    finally:
        state.unregister(websocket)


async def poll_loop():
    """Continuously poll lsof for track changes."""
    log.info(f"Polling Rekordbox every {POLL_INTERVAL}s...")
    while True:
        try:
            await state.poll_once()
        except Exception as e:
            log.error(f"Poll error: {e}")
        await asyncio.sleep(POLL_INTERVAL)


async def playhead_loop():
    """Push live playhead positions from rkbx_link at ~25 Hz."""
    log.info(f"Playhead broadcast every {PLAYHEAD_INTERVAL * 1000:.0f}ms...")
    while True:
        try:
            await state.broadcast_playhead()
        except Exception as e:
            log.error(f"Playhead broadcast error: {e}")
        await asyncio.sleep(PLAYHEAD_INTERVAL)


async def start_mdns(ip: str, port: int):
    """Register the service via mDNS for auto-discovery."""
    from zeroconf.asyncio import AsyncZeroconf
    azc = AsyncZeroconf()
    info = ServiceInfo(
        "_rekordbox-cyd._tcp.local.",
        "Rekordbox CYD Server._rekordbox-cyd._tcp.local.",
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties={"version": "1"},
    )
    try:
        await azc.async_register_service(info, allow_name_change=True)
    except Exception as e:
        log.warning(f"mDNS registration issue (non-fatal): {e}")
    log.info(f"mDNS registered: _rekordbox-cyd._tcp.local on {ip}:{port}")
    return azc


HELP_TEXT = """\
CYD companion app — Track Info server + Auto Cue crossfader relay.

Usage:
  ./run.sh              Start normally
  ./run.sh -d           Start with DDJ-REV5 MIDI diagnostic (prints controls)
  ./run.sh --playhead-debug   Log playhead ticks (pair with CYD Serial)
  ./run.sh -resign      macOS one-time: re-sign Rekordbox for live playhead
  ./run.sh -stop        Kill background rkbx_link / BeatKeeper
  ./run.sh --no-rkbx    Skip rkbx_link (static waveform only)
  ./run.sh -h           Show this help

What runs by default:
  Track Info WebSocket   Port 9100, mDNS auto-discovery for the CYD
  Crossfader relay       DDJ-REV5 CrossFader (B6 1F) → RB-MIDI CC 46 (Auto Cue)
  rkbx_link OSC ingest   UDP 4460 → live playhead (if rkbx_link is running)

Live playhead:
  Prefer ./run.sh — it vendors + starts rkbx_link automatically.
  On macOS, run ./run.sh -resign once before the first live-playhead session.
  Manual: OSC to 127.0.0.1:4460 with osc.msg.n/time true
  (see companion_app/rkbx_link.config.example).

With -d (diagnostic):
  Also listens to the DDJ-REV5 MIDI port and prints every button / knob /
  fader event using Rekordbox-style codes (e.g. B61F, 9007) plus a short
  label. Safe alongside Rekordbox — macOS allows multiple MIDI listeners.

Requirements: DDJ-REV5 connected; for Auto Cue also pair the CYD as RB-MIDI.
"""


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        add_help=False,
        description="Rekordbox CYD companion server",
    )
    parser.add_argument(
        "-d", "--diagnostic",
        action="store_true",
        help="Print live DDJ-REV5 MIDI activity to the terminal",
    )
    parser.add_argument(
        "-p", "--playhead-debug",
        action="store_true",
        help="Log playhead ticks (~5 Hz) for CYD Serial correlation",
    )
    parser.add_argument(
        "-h", "--help",
        action="store_true",
        help="Show help and exit",
    )
    return parser.parse_args(argv)


async def main(diagnostic: bool = False, playhead_debug: bool = False):
    if playhead_debug:
        state.playhead_debug = True
    if state.playhead_debug:
        log.info("Playhead debug ON — look for [PH] lines (pair with CYD Serial)")
    ip = get_local_ip()
    log.info(f"Starting Now-Playing server on {ip}:{WS_PORT}")

    azc = await start_mdns(ip, WS_PORT)

    relay = CrossfaderRelay() if CrossfaderRelay else None
    if relay is not None and not relay.start():
        relay = None
    elif relay is None:
        log.warning("python-rtmidi not installed - Auto Cue relay disabled (pip install python-rtmidi)")

    if RkbxLinkOscReceiver is not None:
        state.rkbx = RkbxLinkOscReceiver()
        if not await state.rkbx.start():
            state.rkbx = None
    else:
        log.warning("rkbx_link_osc unavailable - live playhead disabled")

    diag = None
    if diagnostic:
        if MidiDiagnostic is None:
            log.warning("MIDI diagnostic unavailable (pip install python-rtmidi)")
        else:
            diag = MidiDiagnostic()
            if not diag.start():
                diag = None

    stop = asyncio.Event()
    loop = asyncio.get_event_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop.set)

    async with websockets.serve(ws_handler, "0.0.0.0", WS_PORT):
        log.info(f"WebSocket server listening on ws://0.0.0.0:{WS_PORT}")
        poll_task = asyncio.create_task(poll_loop())
        playhead_task = asyncio.create_task(playhead_loop())
        await stop.wait()
        poll_task.cancel()
        playhead_task.cancel()

    if diag is not None:
        diag.stop()
    if relay is not None:
        relay.stop()
    if state.rkbx is not None:
        state.rkbx.stop()

    await azc.async_unregister_all_services()
    await azc.async_close()
    log.info("Server stopped.")


if __name__ == "__main__":
    args = parse_args()
    if args.help:
        print(HELP_TEXT)
        sys.exit(0)
    asyncio.run(
        main(
            diagnostic=args.diagnostic,
            playhead_debug=args.playhead_debug,
        )
    )
