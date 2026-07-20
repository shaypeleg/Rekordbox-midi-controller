#!/usr/bin/env python3
"""
macOS Rekordbox memory scanner — find BPM / sample-position candidates
for building rkbx_link offsets-macos entries (e.g. Rekordbox 7.2.16).

Prerequisites:
  1. ./run.sh -resign   (get-task-allow on Rekordbox)
  2. Rekordbox running with a track loaded on Deck 1
  3. Run this script with sudo

Examples:
  # Search for a known BPM float, then look nearby for moving sample clocks
  sudo python3 companion_app/scripts/rb_memory_scan.py find-bpm --bpm 128

  # Watch an address while you jog / play (Ctrl+C to stop)
  sudo python3 companion_app/scripts/rb_memory_scan.py watch --addr 0x1234567890 --type f32

  # Dump floats/ints around a candidate
  sudo python3 companion_app/scripts/rb_memory_scan.py near --addr 0x1234567890

  # One-level pointer scan: who points at this address?
  sudo python3 companion_app/scripts/rb_memory_scan.py ptrs --addr 0x1234567890

Typical workflow for a 7.2.16 patch:
  1. find-bpm --bpm <your track BPM>  (optional; many false positives are normal)
  2. live-triage --bpm <BPM>  ← find fresh movers + play/pause test in one run
  3. near --addr <GOOD sample> --bpm <BPM> --radius 0x100  (BPM often +0x68)
  4. ptrs --addr <sample_addr>  → candidate static chains (paste results here)
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Iterator, Optional

# --- Mach / libc bindings -------------------------------------------------

KERN_SUCCESS = 0
TASK_DYLD_INFO = 17
VM_REGION_BASIC_INFO_64 = 9
VM_PROT_READ = 1

libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
libsystem = ctypes.CDLL("/usr/lib/system/libsystem_kernel.dylib", use_errno=True)

mach_port_t = ctypes.c_uint32
kern_return_t = ctypes.c_int
mach_vm_address_t = ctypes.c_uint64
mach_vm_size_t = ctypes.c_uint64
natural_t = ctypes.c_uint32
mach_msg_type_number_t = ctypes.c_uint32
vm_region_flavor_t = ctypes.c_int


class TaskDyldInfo(ctypes.Structure):
    _fields_ = [
        ("all_image_info_addr", ctypes.c_uint64),
        ("all_image_info_size", ctypes.c_uint64),
        ("all_image_info_format", ctypes.c_int32),
    ]


class VmRegionBasicInfo64(ctypes.Structure):
    _fields_ = [
        ("protection", ctypes.c_int32),
        ("max_protection", ctypes.c_int32),
        ("inheritance", ctypes.c_uint32),
        ("shared", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("offset", ctypes.c_uint64),
        ("behavior", ctypes.c_int32),
        ("user_wired_count", ctypes.c_uint16),
    ]


libsystem.mach_task_self.restype = mach_port_t
libsystem.task_for_pid.argtypes = [mach_port_t, ctypes.c_int, ctypes.POINTER(mach_port_t)]
libsystem.task_for_pid.restype = kern_return_t
libsystem.task_info.argtypes = [
    mach_port_t,
    ctypes.c_uint32,
    ctypes.c_void_p,
    ctypes.POINTER(mach_msg_type_number_t),
]
libsystem.task_info.restype = kern_return_t
libsystem.mach_vm_read_overwrite.argtypes = [
    mach_port_t,
    mach_vm_address_t,
    mach_vm_size_t,
    mach_vm_address_t,
    ctypes.POINTER(mach_vm_size_t),
]
libsystem.mach_vm_read_overwrite.restype = kern_return_t
# Reason: modern macOS exports mach_vm_region (not mach_vm_region_64) for
# 64-bit address spaces; signature matches the *64 info flavor.
libsystem.mach_vm_region.argtypes = [
    mach_port_t,
    ctypes.POINTER(mach_vm_address_t),
    ctypes.POINTER(mach_vm_size_t),
    vm_region_flavor_t,
    ctypes.POINTER(VmRegionBasicInfo64),
    ctypes.POINTER(mach_msg_type_number_t),
    ctypes.POINTER(mach_port_t),
]
libsystem.mach_vm_region.restype = kern_return_t


@dataclass
class Region:
    start: int
    size: int
    protection: int


class MachProcess:
    """Minimal task_for_pid + vm read helper for a live Rekordbox process."""

    def __init__(self, pid: int):
        self.pid = pid
        self.task = mach_port_t(0)
        kr = libsystem.task_for_pid(libsystem.mach_task_self(), pid, ctypes.byref(self.task))
        if kr != KERN_SUCCESS:
            raise RuntimeError(
                f"task_for_pid failed ({kr}). Run with sudo after ./run.sh -resign."
            )
        self.base = self._discover_base()

    def _discover_base(self) -> int:
        info = TaskDyldInfo()
        count = mach_msg_type_number_t(
            ctypes.sizeof(TaskDyldInfo) // ctypes.sizeof(natural_t)
        )
        kr = libsystem.task_info(
            self.task, TASK_DYLD_INFO, ctypes.byref(info), ctypes.byref(count)
        )
        if kr != KERN_SUCCESS:
            raise RuntimeError(f"task_info(TASK_DYLD_INFO) failed ({kr})")

        header = self.read(info.all_image_info_addr, 16)
        info_array = struct.unpack_from("<Q", header, 8)[0]
        first = self.read(info_array, 8)
        return struct.unpack_from("<Q", first, 0)[0]

    def read(self, address: int, size: int) -> bytes:
        buf = (ctypes.c_uint8 * size)()
        out = mach_vm_size_t(0)
        kr = libsystem.mach_vm_read_overwrite(
            self.task,
            mach_vm_address_t(address),
            mach_vm_size_t(size),
            ctypes.addressof(buf),
            ctypes.byref(out),
        )
        if kr != KERN_SUCCESS or out.value != size:
            raise OSError(f"read 0x{address:X} size {size} failed kr={kr}")
        return bytes(buf)

    def try_read(self, address: int, size: int) -> Optional[bytes]:
        try:
            return self.read(address, size)
        except OSError:
            return None

    def regions(self) -> Iterator[Region]:
        address = mach_vm_address_t(0)
        while True:
            size = mach_vm_size_t(0)
            info = VmRegionBasicInfo64()
            count = mach_msg_type_number_t(
                ctypes.sizeof(VmRegionBasicInfo64) // ctypes.sizeof(ctypes.c_int32)
            )
            obj = mach_port_t(0)
            kr = libsystem.mach_vm_region(
                self.task,
                ctypes.byref(address),
                ctypes.byref(size),
                VM_REGION_BASIC_INFO_64,
                ctypes.byref(info),
                ctypes.byref(count),
                ctypes.byref(obj),
            )
            if kr != KERN_SUCCESS:
                break
            if info.protection & VM_PROT_READ:
                yield Region(int(address.value), int(size.value), int(info.protection))
            next_addr = int(address.value) + int(size.value)
            if next_addr <= int(address.value):
                break
            address.value = next_addr

    def fmt(self, addr: int) -> str:
        if addr >= self.base:
            return f"0x{addr:X}  (base+0x{addr - self.base:X})"
        return f"0x{addr:X}"


def find_rekordbox_pid() -> int:
    """Prefer the main binary, not rekordboxAgent helpers."""
    try:
        out = subprocess.check_output(["pgrep", "-f", "rekordbox.app/Contents/MacOS/rekordbox"], text=True)
    except subprocess.CalledProcessError as e:
        raise RuntimeError("Rekordbox is not running.") from e
    pids = []
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        pid = int(line)
        try:
            cmd = subprocess.check_output(["ps", "-p", str(pid), "-o", "command="], text=True).strip()
        except subprocess.CalledProcessError:
            continue
        if "rekordboxAgent" in cmd:
            continue
        if cmd.endswith("/rekordbox") or cmd.endswith("MacOS/rekordbox"):
            pids.append(pid)
    if not pids:
        raise RuntimeError("Could not find main rekordbox PID (only Agent?).")
    return pids[0]


def scan_float(
    proc: MachProcess,
    target: float,
    tolerance: float,
    max_hits: int,
) -> list[int]:
    """Scan readable regions for little-endian float32 ≈ target."""
    needle = struct.pack("<f", target)
    hits: list[int] = []
    chunk = 1024 * 1024
    for region in proc.regions():
        # Skip giant GPU/mapped blobs; keep heaps + app data
        if region.size > 256 * 1024 * 1024:
            continue
        offset = 0
        while offset < region.size and len(hits) < max_hits:
            n = min(chunk, region.size - offset)
            data = proc.try_read(region.start + offset, n)
            if data is None:
                offset += n
                continue
            # Also accept nearby floats within tolerance by decoding windows
            for i in range(0, len(data) - 3, 4):
                val = struct.unpack_from("<f", data, i)[0]
                if abs(val - target) <= tolerance:
                    hits.append(region.start + offset + i)
                    if len(hits) >= max_hits:
                        return hits
            offset += n
        if len(hits) >= max_hits:
            break
    # Prefer exact bit-pattern matches first in reporting
    exact = [a for a in hits if proc.try_read(a, 4) == needle]
    rest = [a for a in hits if a not in exact]
    return exact + rest


def read_f32(proc: MachProcess, addr: int) -> Optional[float]:
    b = proc.try_read(addr, 4)
    return None if b is None else struct.unpack("<f", b)[0]


def read_u64(proc: MachProcess, addr: int) -> Optional[int]:
    b = proc.try_read(addr, 8)
    return None if b is None else struct.unpack("<Q", b)[0]


def read_i64(proc: MachProcess, addr: int) -> Optional[int]:
    b = proc.try_read(addr, 8)
    return None if b is None else struct.unpack("<q", b)[0]


def read_f64(proc: MachProcess, addr: int) -> Optional[float]:
    b = proc.try_read(addr, 8)
    return None if b is None else struct.unpack("<d", b)[0]


def cmd_find_bpm(args: argparse.Namespace) -> int:
    pid = find_rekordbox_pid()
    print(f"Rekordbox PID: {pid}")
    proc = MachProcess(pid)
    print(f"Main binary base: 0x{proc.base:X}")
    print(f"Scanning for float32 ≈ {args.bpm} (±{args.tolerance}) …")
    hits = scan_float(proc, args.bpm, args.tolerance, args.max_hits)
    if not hits:
        print("No hits. Check BPM, that a track is loaded, and resign/sudo.")
        return 1

    print(f"Found {len(hits)} candidate(s):\n")
    for addr in hits:
        val = read_f32(proc, addr)
        print(f"  BPM? {proc.fmt(addr)}  value={val}")

    if args.moving:
        print("\nFiltering for values that stay near BPM while neighbors move…")
        print("Play the track for a few seconds (do not change BPM)…")
        time.sleep(args.wait)
        still = []
        for addr in hits:
            val = read_f32(proc, addr)
            if val is not None and abs(val - args.bpm) <= args.tolerance:
                still.append(addr)
        print(f"{len(still)} still look like BPM after {args.wait:.1f}s.")
        hits = still

    if args.near_samples:
        print("\nLooking near each BPM hit for sample-position-like integers…")
        print("(Play/jog the deck; values around 1e5–1e8 that increase are good.)\n")
        for bpm_addr in hits[: args.max_hits]:
            _suggest_sample_near(proc, bpm_addr, args.radius)

    print("\nNext:")
    print("  sudo python3 …/rb_memory_scan.py near --addr <BPM_ADDR>")
    print("  sudo python3 …/rb_memory_scan.py watch --addr <SAMPLE_ADDR> --type u64")
    print("  sudo python3 …/rb_memory_scan.py ptrs --addr <SAMPLE_ADDR>")
    return 0


def _suggest_sample_near(proc: MachProcess, bpm_addr: int, radius: int) -> None:
    start = max(0, bpm_addr - radius)
    data = proc.try_read(start, radius * 2)
    if not data:
        print(f"  (could not read near {proc.fmt(bpm_addr)})")
        return
    print(f"--- near BPM {proc.fmt(bpm_addr)} ---")
    # Reason: rkbx_link reads sample position as i64 at 44100 Hz.
    snap1: dict[int, int] = {}
    for off in range(0, len(data) - 7, 8):
        addr = start + off
        val = struct.unpack_from("<q", data, off)[0]
        if 0 <= val <= 500_000_000:
            snap1[addr] = val
    time.sleep(0.75)
    movers = []
    for addr, v1 in snap1.items():
        v2 = read_i64(proc, addr)
        if v2 is None:
            continue
        delta = v2 - v1
        # ~0.75s of audio at 44.1k ≈ 33k samples; allow wide band
        if 5_000 <= delta <= 200_000:
            movers.append((addr, v1, v2, delta))
    movers.sort(key=lambda t: abs(t[0] - bpm_addr))
    if not movers:
        print("  No clear movers (BPM/sample often not adjacent to false BPM hits).")
        return
    for addr, v1, v2, delta in movers[:12]:
        rel = addr - bpm_addr
        sign = "+" if rel >= 0 else "-"
        print(
            f"  SAMPLE? {proc.fmt(addr)}  "
            f"bpm{sign}0x{abs(rel):X}  {v1} → {v2}  (Δ{delta})"
        )


def _rank_sample_movers(
    movers: list[tuple[int, int, int, int]],
    elapsed: float,
) -> list[tuple[int, int, int, int, str]]:
    """Dedupe movers; annotate likely audio-buffer clocks."""
    target_delta = int(44100 * elapsed)
    movers = sorted(movers, key=lambda t: abs(t[3] - target_delta))
    ranked: list[tuple[int, int, int, int, str]] = []
    seen_vals: set[tuple[int, int]] = set()
    for addr, v1, v2, delta in movers:
        key = (v1, v2)
        note = ""
        if delta > 0 and (delta & (delta - 1)) == 0:
            note = "buffer"
        elif delta in (40960, 81920, 16384, 32768, 65536):
            note = "buffer"
        if key in seen_vals:
            continue
        seen_vals.add(key)
        ranked.append((addr, v1, v2, delta, note))
    return ranked


def _bulk_scan_i64(
    proc: MachProcess,
    min_val: int,
    max_val: int,
    max_region: int,
) -> dict[int, int]:
    """One full pass: addr → i64 for values in range."""
    snap: dict[int, int] = {}
    chunk = 1024 * 1024
    for region in proc.regions():
        if region.size > max_region:
            continue
        offset = 0
        while offset < region.size:
            n = min(chunk, region.size - offset)
            data = proc.try_read(region.start + offset, n)
            if data is None:
                offset += n
                continue
            base = region.start + offset
            for i in range(0, len(data) - 7, 8):
                val = struct.unpack_from("<q", data, i)[0]
                if min_val <= val <= max_val:
                    snap[base + i] = val
            offset += n
    return snap


def _bulk_scan_f64(
    proc: MachProcess,
    min_val: float,
    max_val: float,
    max_region: int,
) -> dict[int, float]:
    """One full pass: addr → f64 for finite values in range (track seconds)."""
    snap: dict[int, float] = {}
    chunk = 1024 * 1024
    for region in proc.regions():
        if region.size > max_region:
            continue
        offset = 0
        while offset < region.size:
            n = min(chunk, region.size - offset)
            data = proc.try_read(region.start + offset, n)
            if data is None:
                offset += n
                continue
            base = region.start + offset
            for i in range(0, len(data) - 7, 8):
                val = struct.unpack_from("<d", data, i)[0]
                if val != val or val in (float("inf"), float("-inf")):
                    continue
                if min_val <= val <= max_val:
                    snap[base + i] = val
            offset += n
    return snap


def _scan_sample_movers(
    proc: MachProcess,
    *,
    wait: float,
    min_val: int,
    max_val: int,
    max_region: int,
) -> list[tuple[int, int, int, int, str]]:
    """
    Two bulk snapshots for i64 clocks advancing ~44100/s.

    Important: do NOT re-read millions of addresses one-by-one after snap1 —
    that makes later addresses look like they advanced for minutes and rejects
    the real playhead. Walk memory the same way twice instead.
    """
    print(
        f"Snapshot 1: bulk-scanning i64 in [{min_val}, {max_val}] …"
    )
    t0 = time.time()
    snap1 = _bulk_scan_i64(proc, min_val, max_val, max_region)
    t1 = time.time()
    print(f"Snapshot 1: {len(snap1)} hits in {t1 - t0:.1f}s. Waiting {wait:.2f}s …")
    time.sleep(wait)

    print("Snapshot 2: bulk-scanning again (same walk order) …")
    t2 = time.time()
    snap2 = _bulk_scan_i64(proc, min_val, max_val, max_region)
    t3 = time.time()
    # Reason: same region order ⇒ per-address Δt ≈ wait + avg(scan1, scan2).
    elapsed = wait + 0.5 * ((t1 - t0) + (t3 - t2))
    print(
        f"Snapshot 2: {len(snap2)} hits in {t3 - t2:.1f}s. "
        f"Effective Δt ≈ {elapsed:.2f}s"
    )

    rate_lo = int(44100 * elapsed * 0.45)
    rate_hi = int(44100 * elapsed * 1.65)
    movers: list[tuple[int, int, int, int]] = []
    for addr, v1 in snap1.items():
        v2 = snap2.get(addr)
        if v2 is None:
            continue
        delta = v2 - v1
        if rate_lo <= delta <= rate_hi:
            movers.append((addr, v1, v2, delta))

    print(
        f"Found {len(movers)} raw mover(s) at ~44100/s "
        f"(Δ band {rate_lo}…{rate_hi})."
    )
    return _rank_sample_movers(movers, elapsed)


def _scan_time_movers(
    proc: MachProcess,
    *,
    wait: float,
    min_val: float,
    max_val: float,
    max_region: int,
) -> list[tuple[int, float, float, float, str]]:
    """Two bulk snapshots for f64 track-time clocks advancing ~1.0 s/s."""
    print(f"Snapshot 1: bulk-scanning f64 seconds in [{min_val}, {max_val}] …")
    t0 = time.time()
    snap1 = _bulk_scan_f64(proc, min_val, max_val, max_region)
    t1 = time.time()
    print(f"Snapshot 1: {len(snap1)} hits in {t1 - t0:.1f}s. Waiting {wait:.2f}s …")
    time.sleep(wait)

    print("Snapshot 2: bulk-scanning f64 again …")
    t2 = time.time()
    snap2 = _bulk_scan_f64(proc, min_val, max_val, max_region)
    t3 = time.time()
    elapsed = wait + 0.5 * ((t1 - t0) + (t3 - t2))
    print(
        f"Snapshot 2: {len(snap2)} hits in {t3 - t2:.1f}s. "
        f"Effective Δt ≈ {elapsed:.2f}s"
    )

    lo = elapsed * 0.45
    hi = elapsed * 1.65
    movers: list[tuple[int, float, float, float]] = []
    for addr, v1 in snap1.items():
        v2 = snap2.get(addr)
        if v2 is None:
            continue
        delta = v2 - v1
        if lo <= delta <= hi:
            movers.append((addr, v1, v2, delta))

    movers.sort(key=lambda t: abs(t[3] - elapsed))
    ranked: list[tuple[int, float, float, float, str]] = []
    seen: set[tuple[float, float]] = set()
    for addr, v1, v2, delta in movers:
        key = (round(v1, 3), round(v2, 3))
        if key in seen:
            continue
        seen.add(key)
        ranked.append((addr, v1, v2, delta, ""))
    print(f"Found {len(ranked)} unique f64 time mover(s) (Δ band {lo:.2f}…{hi:.2f}).")
    return ranked


def cmd_find_samples(args: argparse.Namespace) -> int:
    """
    Global differential scan for i64 playhead candidates (~44100 samples/s).

    Prefer this over --near-samples: deck BPM floats are drowned out by
    hundreds of unrelated 92.0 values, and the real sample clock may not sit
    next to any of those false BPM hits.
    """
    pid = find_rekordbox_pid()
    print(f"Rekordbox PID: {pid}")
    proc = MachProcess(pid)
    print(f"Main binary base: 0x{proc.base:X}")
    print("Keep the track PLAYING (ideally mid-track, not at 0:00).\n")

    ranked = _scan_sample_movers(
        proc,
        wait=args.wait,
        min_val=args.min_val,
        max_val=args.max_val,
        max_region=args.max_region,
    )
    print(f"Unique clocks: {len(ranked)} (showing up to {args.max_hits}):\n")
    if not ranked:
        print(
            "None found. Confirm: track is playing (not paused), seek past 0:00,\n"
            "resign is still valid, and wait was long enough."
        )
        return 1

    for addr, v1, v2, delta, note in ranked[: args.max_hits]:
        rate = delta / args.wait
        tag = f"  [{note}]" if note else ""
        print(
            f"  SAMPLE? {proc.fmt(addr)}  {v1} → {v2}  "
            f"(Δ{delta}, ~{rate:.0f}/s){tag}"
        )

    print("\nPrefer: live-triage (finds fresh addresses + play/pause in one run)")
    print("  sudo python3 companion_app/scripts/rb_memory_scan.py live-triage --bpm 92")
    return 0


def cmd_live_triage(args: argparse.Namespace) -> int:
    """
    Find lasting playhead clocks (i64 samples, then f64 seconds fallback),
    hot-filter, then play/pause-test survivors.
    """
    pid = find_rekordbox_pid()
    print(f"Rekordbox PID: {pid}")
    proc = MachProcess(pid)
    print(f"Main binary base: 0x{proc.base:X}")
    print()
    print("=" * 50)
    print(">>> START PLAYING NOW (mid-track) and KEEP PLAYING <<<")
    print(f">>> Scan starts in {args.prep:.0f}s… <<<")
    print("=" * 50)
    time.sleep(args.prep)

    mode = "i64"
    ranked_i = _scan_sample_movers(
        proc,
        wait=args.wait,
        min_val=args.min_val,
        max_val=args.max_val,
        max_region=args.max_region,
    )
    pool_addrs = [r[0] for r in ranked_i if r[4] != "buffer"][: args.pool]
    if len(pool_addrs) < min(32, args.pool):
        pool_addrs = [r[0] for r in ranked_i][: args.pool]

    def hot_filter_i64(addrs: list[int]) -> list[tuple[int, float, float, float, str]]:
        print(
            f"\nHot-filter i64: re-checking {len(addrs)} candidates "
            f"for {args.hot_wait:.1f}s…"
        )
        snap = {a: read_i64(proc, a) for a in addrs}
        time.sleep(args.hot_wait)
        lo = 44100 * args.hot_wait * 0.50
        hi = 44100 * args.hot_wait * 1.60
        hot: list[tuple[int, float, float, float, str]] = []
        for a in addrs:
            v1, v2 = snap.get(a), read_i64(proc, a)
            if v1 is None or v2 is None:
                continue
            d = float(v2 - v1)
            if lo <= d <= hi:
                bpm_note = ""
                for rel in (0x68, -0x68):
                    fv = read_f32(proc, a + rel)
                    if fv is not None and abs(fv - args.bpm) <= 0.05:
                        bpm_note = f"bpm@{rel:+#x}"
                        break
                hot.append((a, float(v1), float(v2), d, bpm_note))
        hot.sort(
            key=lambda t: (0 if t[4] else 1, abs(t[3] - 44100 * args.hot_wait))
        )
        return hot

    def hot_filter_f64(addrs: list[int]) -> list[tuple[int, float, float, float, str]]:
        print(
            f"\nHot-filter f64: re-checking {len(addrs)} candidates "
            f"for {args.hot_wait:.1f}s…"
        )
        snap = {a: read_f64(proc, a) for a in addrs}
        time.sleep(args.hot_wait)
        lo = args.hot_wait * 0.50
        hi = args.hot_wait * 1.60
        hot: list[tuple[int, float, float, float, str]] = []
        for a in addrs:
            v1, v2 = snap.get(a), read_f64(proc, a)
            if v1 is None or v2 is None:
                continue
            d = v2 - v1
            if lo <= d <= hi:
                bpm_note = ""
                for rel in (0x68, -0x68, 0x8, -0x8, 0x10, -0x10):
                    fv = read_f32(proc, a + rel)
                    if fv is not None and abs(fv - args.bpm) <= 0.05:
                        bpm_note = f"bpm@{rel:+#x}"
                        break
                hot.append((a, v1, v2, d, bpm_note))
        hot.sort(key=lambda t: (0 if t[4] else 1, abs(t[3] - args.hot_wait)))
        return hot

    hot = hot_filter_i64(pool_addrs) if pool_addrs else []
    print(f"Still-hot i64 clocks: {len(hot)}")

    if not hot:
        print("\nNo lasting i64 sample clocks — trying f64 seconds scan…")
        mode = "f64"
        ranked_f = _scan_time_movers(
            proc,
            wait=args.wait,
            min_val=5.0,
            max_val=60.0 * 30.0,
            max_region=args.max_region,
        )
        pool_addrs = [r[0] for r in ranked_f][: args.pool]
        if not pool_addrs:
            print("No f64 movers either. Abort.")
            return 1
        hot = hot_filter_f64(pool_addrs)
        print(f"Still-hot f64 clocks: {len(hot)}")

    if not hot:
        print(
            "All movers died in hot-filter. Keep audio playing mid-track and re-run."
        )
        return 1

    picks = hot[: args.top]
    addrs = [p[0] for p in picks]
    print(f"\nPause-testing top {len(addrs)} still-hot {mode} candidate(s):")
    for addr, v1, v2, delta, bpm_note in picks:
        extra = f"  [{bpm_note}]" if bpm_note else ""
        print(f"  {proc.fmt(addr)}  {v1:g} → {v2:g}  hotΔ{delta:g}{extra}")

    def read_val(a: int) -> Optional[float]:
        if mode == "i64":
            v = read_i64(proc, a)
            return None if v is None else float(v)
        return read_f64(proc, a)

    def sample_many(label: str) -> dict[int, list[Optional[float]]]:
        print(f"\n--- {label} ({args.sample:.1f}s) ---")
        series: dict[int, list[Optional[float]]] = {a: [] for a in addrs}
        t0 = time.time()
        move_thresh = 1000.0 if mode == "i64" else 0.05
        while time.time() - t0 < args.sample:
            for a in addrs:
                series[a].append(read_val(a))
            moved = sum(
                1
                for a in addrs
                if len(series[a]) >= 2
                and series[a][-1] is not None
                and series[a][0] is not None
                and abs(series[a][-1] - series[a][0]) > move_thresh  # type: ignore[operator]
            )
            print(
                f"  t={time.time() - t0:4.1f}s  "
                f"first={series[addrs[0]][-1]}  still_moving={moved}/{len(addrs)}"
            )
            time.sleep(args.interval)
        return series

    print("\nKeep PLAYING…")
    play_series = sample_many("PLAYING")

    print()
    print("=" * 50)
    print(">>> PAUSE THE TRACK NOW <<<")
    print(f">>> Pause sampling in {args.prep:.0f}s… <<<")
    print("=" * 50)
    time.sleep(args.prep)
    pause_series = sample_many("PAUSED")

    play_good = 20_000.0 if mode == "i64" else 0.4
    pause_freeze = 500.0 if mode == "i64" else 0.05

    print("\n" + "=" * 50)
    print(f"RESULTS ({mode})")
    goods: list[int] = []
    for a in addrs:
        pv = [v for v in play_series[a] if v is not None]
        qv = [v for v in pause_series[a] if v is not None]
        play_d = (pv[-1] - pv[0]) if len(pv) >= 2 else None
        pause_d = (qv[-1] - qv[0]) if len(qv) >= 2 else None
        verdict = "UNCLEAR"
        if play_d is not None and pause_d is not None:
            if play_d > play_good and abs(pause_d) < pause_freeze:
                verdict = "GOOD"
                goods.append(a)
            elif play_d > play_good and abs(pause_d) > play_good:
                verdict = "BAD (moves while paused)"
            elif abs(play_d) < (1000.0 if mode == "i64" else 0.05):
                verdict = "DEAD/stale during play sample"
        print(
            f"  {proc.fmt(a)}  playΔ={play_d}  pauseΔ={pause_d}  → {verdict}"
        )
        if pv:
            print(f"    play values:  {pv[0]:g} … {pv[-1]:g}")
        if qv:
            print(f"    pause values: {qv[0]:g} … {qv[-1]:g}")
    print("=" * 50)

    if goods:
        print(f"\n{len(goods)} GOOD candidate(s). Next for each:")
        for a in goods:
            print(
                f"  sudo python3 companion_app/scripts/rb_memory_scan.py "
                f"near --addr 0x{a:X} --bpm {args.bpm:g} --radius 0x100"
            )
            wtype = "i64" if mode == "i64" else "f64"
            print(
                f"  sudo python3 companion_app/scripts/rb_memory_scan.py "
                f"watch --addr 0x{a:X} --type {wtype}"
            )
        return 0

    print(
        "\nNo GOOD candidates among still-hot clocks. Paste RESULTS here.\n"
        "Tip: pause exactly when prompted; keep playing through both scans."
    )
    return 1


def cmd_near(args: argparse.Namespace) -> int:
    proc = MachProcess(find_rekordbox_pid())
    addr = args.addr
    radius = args.radius
    start = max(0, addr - radius)
    data = proc.try_read(start, radius * 2)
    if data is None:
        print("Read failed.")
        return 1
    print(f"Dump ±0x{radius:X} around {proc.fmt(addr)} (base 0x{proc.base:X})\n")
    # Reason: Mac 7.2.8 deck object has sample @ +0x120 and BPM @ +0x188 (+0x68).
    if args.bpm:
        for rel in (0x68, -0x68, 0x188 - 0x120, 0x120 - 0x188):
            a = addr + rel
            v = read_f32(proc, a)
            if v is not None and abs(v - args.bpm) <= 0.05:
                print(
                    f"*** BPM-like float at sample{rel:+#x}: {proc.fmt(a)} = {v} "
                    f"(strong deck-object signal)\n"
                )

    for off in range(0, len(data) - 7, 8):
        a = start + off
        u64 = struct.unpack_from("<Q", data, off)[0]
        i64 = struct.unpack_from("<q", data, off)[0]
        f32a = struct.unpack_from("<f", data, off)[0]
        f32b = struct.unpack_from("<f", data, off + 4)[0]
        mark = " <<" if a == addr else ""
        interesting = ""
        if args.bpm and (abs(f32a - args.bpm) < 0.05 or abs(f32b - args.bpm) < 0.05):
            interesting = "  [BPM-like float]"
        elif 10_000 <= i64 <= 500_000_000:
            interesting = "  [sample-like i64]"
        if interesting or mark:
            print(
                f"  {proc.fmt(a)}  i64={i64:<12}  "
                f"f32={f32a:g}/{f32b:g}{interesting}{mark}"
            )
    return 0


def cmd_triage(args: argparse.Namespace) -> int:
    """
    Guided play/pause test for one sample-position candidate.

    Prints countdowns so you know when to play and pause; then reports
    whether the address behaves like a deck playhead.
    """
    proc = MachProcess(find_rekordbox_pid())
    addr = args.addr
    print(f"PID={proc.pid}  watching {proc.fmt(addr)} as i64\n")

    def sample(label: str, seconds: float) -> list[Optional[int]]:
        print(f"--- {label} ---")
        vals: list[Optional[int]] = []
        t0 = time.time()
        while time.time() - t0 < seconds:
            v = read_i64(proc, addr)
            vals.append(v)
            print(f"  t={time.time() - t0:4.1f}s  value={v}")
            time.sleep(args.interval)
        return vals

    def delta(vals: list[Optional[int]]) -> Optional[int]:
        ok = [v for v in vals if v is not None]
        if len(ok) < 2:
            return None
        return ok[-1] - ok[0]

    print("=" * 50)
    print(">>> START PLAYING THE TRACK NOW (mid-track) <<<")
    print(f">>> You have {args.prep:.0f} seconds… <<<")
    print("=" * 50)
    time.sleep(args.prep)
    play = sample("PLAYING", args.sample)
    play_d = delta(play)

    print()
    print("=" * 50)
    print(">>> PAUSE THE TRACK NOW <<<")
    print(f">>> You have {args.prep:.0f} seconds… <<<")
    print("=" * 50)
    time.sleep(args.prep)
    paused = sample("PAUSED", args.sample)
    pause_d = delta(paused)

    play_vals = [v for v in play if v is not None]
    pause_vals = [v for v in paused if v is not None]
    print()
    print("=" * 50)
    print("RESULT")
    print(f"  PLAYING Δ ≈ {play_d}  values {play_vals[:1]} … {play_vals[-1:]}")
    print(f"  PAUSED  Δ ≈ {pause_d}  values {pause_vals[:1]} … {pause_vals[-1:]}")
    if play_d is not None and pause_d is not None:
        if play_d > 20_000 and abs(pause_d) < 500:
            print("  VERDICT: GOOD — moves when playing, freezes when paused")
        elif play_d > 20_000 and pause_d > 20_000:
            print("  VERDICT: BAD — keeps moving while paused (not deck playhead)")
        elif play_d < 1000 and pause_d < 1000:
            print(
                "  VERDICT: STALE ADDRESS — heap moved; use live-triage instead:\n"
                "    sudo python3 companion_app/scripts/rb_memory_scan.py "
                "live-triage --bpm 92"
            )
        else:
            print("  VERDICT: UNCLEAR — re-run; ensure play then pause on cue")
    print("=" * 50)
    if play_d is not None and pause_d is not None and play_d > 20_000 and abs(pause_d) < 500:
        print("\nNext:")
        print(
            f"  sudo python3 companion_app/scripts/rb_memory_scan.py "
            f"near --addr 0x{addr:X} --bpm 92 --radius 0x100"
        )
    return 0


def cmd_watch(args: argparse.Namespace) -> int:
    proc = MachProcess(find_rekordbox_pid())
    addr = args.addr
    print(f"Watching {proc.fmt(addr)} as {args.type} (Ctrl+C to stop)")
    try:
        while True:
            if args.type == "f32":
                v = read_f32(proc, addr)
            elif args.type == "u64":
                v = read_u64(proc, addr)
            elif args.type == "f64":
                v = read_f64(proc, addr)
            else:
                v = read_i64(proc, addr)
            print(f"\r  {v}          ", end="", flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nStopped.")
    return 0


def _find_ptr_hits(
    proc: MachProcess,
    target: int,
    max_hits: int,
) -> list[int]:
    return _find_ptr_hits_multi(proc, [target], max_hits).get(target, [])


def _find_ptr_hits_multi(
    proc: MachProcess,
    targets: list[int],
    max_hits: int,
) -> dict[int, list[int]]:
    """One memory walk; find aligned u64 pointers equal to any target."""
    uniq = list(dict.fromkeys(targets))
    needles = {t: struct.pack("<Q", t) for t in uniq}
    hits: dict[int, list[int]] = {t: [] for t in uniq}
    done = 0
    chunk = 1024 * 1024
    for region in proc.regions():
        if region.size > 256 * 1024 * 1024:
            continue
        if done >= len(uniq):
            break
        offset = 0
        while offset < region.size:
            n = min(chunk, region.size - offset)
            data = proc.try_read(region.start + offset, n)
            if data is None:
                offset += n
                continue
            base = region.start + offset
            for t, needle in needles.items():
                if len(hits[t]) >= max_hits:
                    continue
                start_i = 0
                while True:
                    i = data.find(needle, start_i)
                    if i < 0:
                        break
                    if i % 8 == 0:
                        hits[t].append(base + i)
                        if len(hits[t]) >= max_hits:
                            break
                    start_i = i + 8
            done = sum(1 for t in uniq if len(hits[t]) >= max_hits)
            offset += n
    return hits


def _classify_ptr_hits(
    proc: MachProcess, hits: list[int]
) -> tuple[list[int], list[int]]:
    in_image: list[int] = []
    elsewhere: list[int] = []
    for a in hits:
        if proc.base <= a < proc.base + 0x10000000:
            in_image.append(a)
        else:
            elsewhere.append(a)
    return in_image, elsewhere


def _follow_rkbx_pointer(
    proc: MachProcess,
    offsets: list[int],
    final_offset: int,
) -> Optional[int]:
    """
    Resolve like rkbx_link: address=base; for o in offsets: address=read(address+o);
    then address += final_offset (no deref).
    """
    addr = proc.base
    for off in offsets:
        val = read_u64(proc, addr + off)
        if val is None or val < 0x100000000:
            return None
        addr = val
    return addr + final_offset


def _is_heapish(addr: int, proc: MachProcess) -> bool:
    """Rough filter for user-space heap pointers on Apple Silicon."""
    if addr < 0x100000000 or addr > 0x2000000000:
        return False
    # Exclude the main binary image itself
    if proc.base <= addr < proc.base + 0x10000000:
        return False
    return True


def _scan_ptrs_into_range(
    proc: MachProcess,
    lo: int,
    hi: int,
    max_hits: int,
) -> list[tuple[int, int]]:
    """
    Find aligned u64 values in [lo, hi] (pointer into a range).
    Returns list of (slot_addr, pointed_value).
    """
    hits: list[tuple[int, int]] = []
    chunk = 1024 * 1024
    for region in proc.regions():
        if region.size > 256 * 1024 * 1024:
            continue
        offset = 0
        while offset < region.size and len(hits) < max_hits:
            n = min(chunk, region.size - offset)
            data = proc.try_read(region.start + offset, n)
            if data is None:
                offset += n
                continue
            base = region.start + offset
            for i in range(0, len(data) - 7, 8):
                val = struct.unpack_from("<Q", data, i)[0]
                if lo <= val <= hi:
                    hits.append((base + i, val))
                    if len(hits) >= max_hits:
                        return hits
            offset += n
    return hits


def cmd_trace_up(args: argparse.Namespace) -> int:
    """
    Upward pointer search from a GOOD sample toward the main binary.

    Strategy: find exact parents of the object, then scan for pointers into
    the parent *cluster* (vector/array bases), not only exact field addresses.
    """
    proc = MachProcess(find_rekordbox_pid())
    sample = args.addr
    obj_base = sample - 0x120

    print(f"Sample:      {proc.fmt(sample)}")
    print(f"Object base: {proc.fmt(obj_base)}")

    bpm = read_f32(proc, sample + 0x68)
    pos = read_i64(proc, sample)
    print(f"Freshness:   sample i64={pos}  bpm@+68={bpm}")
    if bpm is None or abs(bpm - args.bpm) > 0.05:
        print(
            "\n*** STALE ADDRESS *** BPM@+0x68 is not your track BPM.\n"
            "Re-run live-triage and use the new GOOD address with trace-up."
        )
        return 1
    if pos == 0:
        print(
            "Note: sample is 0 — press PLAY mid-track (BPM match still OK)."
        )

    print("\n=== Level 1: exact pointers to object base ===")
    d1 = _find_ptr_hits(proc, obj_base, args.max_hits)
    d1_range = _scan_ptrs_into_range(proc, obj_base, obj_base + 0x1C0, args.max_hits)
    slots = sorted(set(d1 + [s for s, _ in d1_range]))
    in1, heap_raw = _classify_ptr_hits(proc, slots)

    def _is_stackish(a: int) -> bool:
        # Reason: Apple Silicon thread stacks often live in 0x16xxxxxxx–0x17xxxxxxx
        return 0x160000000 <= a < 0x180000000

    def _in_object(a: int) -> bool:
        return obj_base <= a < obj_base + 0x400

    heap1 = [a for a in heap_raw if not _is_stackish(a) and not _in_object(a)]
    skipped = len(heap_raw) - len(heap1)
    print(f"  exact={len(d1)} range={len(d1_range)} unique_slots={len(slots)}")
    print(f"  image={len(in1)} heap={len(heap1)} (skipped stack/self={skipped})")
    for a in in1[:20]:
        print(f"  *** IMAGE L1 {proc.fmt(a)}")
    for a in heap1[:12]:
        print(f"  HEAP L1 {proc.fmt(a)}")

    image_leads: list[tuple[int, str]] = [(a, "L1→object") for a in in1]

    if not heap1 and not in1:
        print("No parents. Re-run live-triage for a fresh address.")
        return 1

    def _tight_clusters(addrs: list[int], max_span: int = 0x200000) -> list[tuple[int, int, list[int]]]:
        """
        Group addresses into tight clusters (span <= max_span).
        Returns list of (lo, hi, members) sorted by size descending.
        """
        if not addrs:
            return []
        s = sorted(addrs)
        groups: list[list[int]] = [[s[0]]]
        for a in s[1:]:
            if a - groups[-1][0] <= max_span:
                groups[-1].append(a)
            else:
                groups.append([a])
        clusters = [(g[0], g[-1], g) for g in groups]
        clusters.sort(key=lambda c: (-len(c[2]), c[0]))
        return clusters

    def _level_into_cluster(
        level: int,
        members: list[int],
        label: str,
    ) -> list[int]:
        """Scan for external pointers into a tight cluster; return new heap slots."""
        clusters = _tight_clusters(members)
        if not clusters:
            return []
        lo, hi, group = clusters[0]
        scan_lo = lo & ~0xFFF
        scan_hi = (hi + 0xFFF) & ~0xFFF
        span = scan_hi - scan_lo
        print(
            f"\n=== Level {level}: pointers into {label} cluster "
            f"0x{scan_lo:X}..0x{scan_hi:X} "
            f"(n={len(group)}, span=0x{span:X}) ==="
        )
        if span > 0x800000:
            print("  cluster still too wide — aborting this level")
            return []

        into = _scan_ptrs_into_range(proc, scan_lo, scan_hi, args.max_hits * 4)
        # Keep hits whose value is near a real member (not random addr in span)
        member_set = set(group)

        def near_member(v: int) -> bool:
            if v in member_set:
                return True
            for m in group:
                if abs(v - m) <= 0x200:
                    return True
            return False

        external = [
            (s, v)
            for s, v in into
            if near_member(v) and not (scan_lo <= s <= scan_hi + 0x4000)
            and not _is_stackish(s)
        ]
        in_img, heap_next = _classify_ptr_hits(proc, [s for s, _ in external])
        print(f"  into={len(into)} filtered_external={len(external)}")
        print(f"  external image={len(in_img)} heap={len(heap_next)}")
        for a in in_img[:40]:
            print(f"  *** IMAGE L{level} {proc.fmt(a)}  (static {a - proc.base:X})")
            image_leads.append((a, f"L{level}→{label}"))
        for a in heap_next[:15]:
            print(f"  HEAP L{level} {proc.fmt(a)}")
        return heap_next

    if heap1:
        heap2 = _level_into_cluster(2, heap1, "L1")
        if heap2 and not any(v.startswith("L2") for _, v in image_leads):
            heap3 = _level_into_cluster(3, heap2, "L2")
            if heap3 and not any(v.startswith("L3") for _, v in image_leads):
                _level_into_cluster(4, heap3, "L3")

    print("\n=== IMAGE LEADS ===")
    if not image_leads:
        print("  (none reached the main binary)")
        print(
            "  Try: play mid-track, re-run live-triage for a fresh GOOD address,\n"
            "  then immediately: trace-up --addr <NEW> --bpm 92 --max-hits 200"
        )
        return 1

    seen = set()
    for slot, via in image_leads:
        if slot in seen:
            continue
        seen.add(slot)
        static = slot - proc.base
        print(f"\n  STATIC 0x{static:X}  ({via})")
        synthesized = _synthesize_chain(proc, static, obj_base, sample)
        if synthesized:
            for line in synthesized:
                print(f"    TRY: {line}")
        else:
            # Wider synthesize: also try final offsets via value at static
            print("    (no short chain yet — static offset still useful)")
            print(f"    value@static = {read_u64(proc, slot)!r}")

    print("\nPaste this output into the chat.")
    return 0


def _synthesize_chain(
    proc: MachProcess,
    static_off: int,
    obj_base: int,
    sample: int,
) -> list[str]:
    """Brute small hop sets from a known static slot to obj_base/sample."""
    out: list[str] = []
    needle = obj_base
    p0 = read_u64(proc, proc.base + static_off)
    if p0 is None:
        return out
    if p0 == obj_base:
        out.append(f"{static_off:X} 120")
        out.append(f"{static_off:X} 188")
        return out

    # 1 hop
    block = proc.try_read(p0, 0x800)
    if block:
        for i in range(0, len(block) - 7, 8):
            if struct.unpack_from("<Q", block, i)[0] == needle:
                out.append(f"{static_off:X} {i:X} 120")
                out.append(f"{static_off:X} {i:X} 188")
                leaf = _follow_rkbx_pointer(proc, [static_off, i], 0x120)
                if leaf == sample:
                    out.append(f"  VERIFIED sample match via {static_off:X} {i:X} 120")
                return out

    # 2 hops (deck select style)
    for hop in (0x0, 0x8, 0x10, 0x18):
        p1 = read_u64(proc, p0 + hop)
        if p1 is None or not _is_heapish(p1, proc):
            continue
        block = proc.try_read(p1, 0x800)
        if not block:
            continue
        for i in range(0, len(block) - 7, 8):
            if struct.unpack_from("<Q", block, i)[0] == needle:
                out.append(f"{static_off:X} {hop:X} {i:X} 120")
                out.append(f"{static_off:X} {hop:X} {i:X} 188")
                leaf = _follow_rkbx_pointer(proc, [static_off, hop, i], 0x120)
                if leaf == sample:
                    out.append(
                        f"  VERIFIED sample match via "
                        f"{static_off:X} {hop:X} {i:X} 120"
                    )
                return out

    # 3 hops
    for hop1 in (0x0, 0x8, 0x10, 0x18):
        p1 = read_u64(proc, p0 + hop1)
        if p1 is None or not _is_heapish(p1, proc):
            continue
        for hop2 in (0x0, 0x8, 0x10, 0x18, 0x20, 0x28, 0x30):
            p2 = read_u64(proc, p1 + hop2)
            if p2 is None or not _is_heapish(p2, proc):
                continue
            block = proc.try_read(p2, 0x800)
            if not block:
                continue
            for i in range(0, len(block) - 7, 8):
                if struct.unpack_from("<Q", block, i)[0] == needle:
                    out.append(f"{static_off:X} {hop1:X} {hop2:X} {i:X} 120")
                    leaf = _follow_rkbx_pointer(
                        proc, [static_off, hop1, hop2, i], 0x120
                    )
                    if leaf == sample:
                        out.append("  VERIFIED sample match")
                    return out
    return out


def cmd_probe_hops(args: argparse.Namespace) -> int:
    """
    Try many deck-hop values for STATIC … MID 120/188 and report sane hits.

    Load a known-BPM track on the deck you care about (and play it), then run.
    """
    proc = MachProcess(find_rekordbox_pid())
    static = args.static
    mid = args.mid
    print(f"Probing hops for {static:X} <hop> {mid:X} 120 / 188")
    print(f"Looking for BPM ≈ {args.bpm} and a plausible sample i64…\n")
    hits = []
    for hop in range(0, args.max_hop + 1, 8):
        sample_a = _follow_rkbx_pointer(proc, [static, hop, mid], 0x120)
        bpm_a = _follow_rkbx_pointer(proc, [static, hop, mid], 0x188)
        if sample_a is None or bpm_a is None:
            continue
        bpm = read_f32(proc, bpm_a)
        pos = read_i64(proc, sample_a)
        if bpm is None or pos is None:
            continue
        if abs(bpm - args.bpm) > 0.05:
            continue
        if not (0 <= pos <= 500_000_000):
            continue
        hits.append((hop, sample_a, pos, bpm))
        print(
            f"  HIT hop=0x{hop:X}  sample={proc.fmt(sample_a)}  "
            f"i64={pos}  bpm={bpm}"
        )
        print(f"       offsets: {static:X} {hop:X} {mid:X} 120")
        print(f"                {static:X} {hop:X} {mid:X} 188")

    if not hits:
        print("No hops matched. Play a known-BPM track and re-run.")
        return 1

    print(
        f"\n{len(hits)} hop(s) matched. "
        "Load the same BPM on other decks and re-run to map 1–4."
    )
    return 0


def cmd_verify_chain(args: argparse.Namespace) -> int:
    """Resolve an rkbx_link-style chain and print sample/BPM values."""
    proc = MachProcess(find_rekordbox_pid())
    parts = [int(x, 16) for x in args.chain.split()]
    if len(parts) < 2:
        print("Need at least STATIC FINAL (e.g. B31CC40 8 550 120)")
        return 1
    offsets, final = parts[:-1], parts[-1]
    addr = _follow_rkbx_pointer(proc, offsets, final)
    print(f"Chain: {' '.join(f'{p:X}' for p in parts)}")
    print(f"Base:  0x{proc.base:X}")
    if addr is None:
        print("RESOLVE FAILED")
        return 1
    print(f"Leaf:  {proc.fmt(addr)}")
    if final == 0x120 or args.as_sample:
        print(f"i64:   {read_i64(proc, addr)}")
        bpm_addr = _follow_rkbx_pointer(proc, offsets, 0x188)
        if bpm_addr:
            print(f"BPM@188 {proc.fmt(bpm_addr)} = {read_f32(proc, bpm_addr)}")
    elif final == 0x188:
        print(f"f32:   {read_f32(proc, addr)}")
    else:
        print(f"i64:   {read_i64(proc, addr)}")
        print(f"f32:   {read_f32(proc, addr)}")
        print(f"f64:   {read_f64(proc, addr)}")
    return 0


def cmd_find_chain(args: argparse.Namespace) -> int:
    """
    Forward-search rkbx_link-style pointer chains from the main binary that
    resolve to the deck object / sample field.
    """
    proc = MachProcess(find_rekordbox_pid())
    sample = args.addr
    obj_base = sample - 0x120
    print(f"Sample:      {proc.fmt(sample)}")
    print(f"Object base: {proc.fmt(obj_base)}")
    print(f"Main base:   0x{proc.base:X}")
    bpm = read_f32(proc, sample + 0x68)
    print(f"Freshness:   bpm@+68={bpm}")
    if bpm is None or abs(bpm - args.bpm) > 0.05:
        print(
            "*** STALE? BPM@+0x68 mismatch. Re-run live-triage, then find-chain "
            "or trace-up with the new address."
        )
        return 1

    print("\n=== Probe corrected 7.2.8 chain: offsets [04D8AA18, 0, 2C8] final 120 ===")
    leaf = _follow_rkbx_pointer(proc, [0x04D8AA18, 0x0, 0x2C8], 0x120)
    if leaf is None:
        print("  unreadable (static shifted — expected)")
    else:
        print(f"  → {proc.fmt(leaf)}  i64={read_i64(proc, leaf)}")
        if leaf == sample:
            print("  *** EXACT MATCH ***")

    # Bulk-read main binary image for static pointer candidates
    image_size = args.image_size
    print(f"\nReading main image ({image_size // (1024 * 1024)} MiB) for static ptrs…")
    image = proc.try_read(proc.base, image_size)
    if not image:
        print("Could not read main image.")
        return 1

    static_ptrs: list[tuple[int, int]] = []  # (static_offset, heap_ptr)
    for i in range(0, len(image) - 7, 8):
        val = struct.unpack_from("<Q", image, i)[0]
        if _is_heapish(val, proc):
            static_ptrs.append((i, val))
    print(f"Static heap-like pointers: {len(static_ptrs)}")

    # --- Direct: read(base+S) == obj_base → chain "S 120"
    print("\n=== Direct chains: STATIC → object (then +120 = sample) ===")
    direct = [(s, p) for s, p in static_ptrs if p == obj_base]
    if not direct:
        print("  (none)")
    for s, p in direct[:20]:
        print(f"  GOOD: {s:X} 120")
        print(f"        BPM:  {s:X} 188")

    # Mid offsets to try (deck table hops + 7.2.8 mid + neighborhood)
    mids = list(
        dict.fromkeys(
            [
                0x0,
                0x8,
                0x10,
                0x18,
                0x20,
                0x28,
                0x30,
                0x38,
                0x40,
                0x48,
                0x50,
                0x60,
                0x70,
                0x80,
                0x100,
                0x200,
                0x270,
                0x278,
                0x2C0,
                0x2C8,
                0x2D0,
                0x300,
                0x3F0,
                *range(0, 0x400, 8),
            ]
        )
    )

    # --- One hop: read(base+S)=P1; read(P1+MID)==obj_base → "S MID 120"
    max_mid = max(mids)
    print(
        f"\n=== One-hop chains: STATIC → +MID → object "
        f"(block read 0x{max_mid + 8:X} per static) ==="
    )
    one_hop: list[tuple[int, int]] = []
    checked = 0
    chase = static_ptrs[: args.max_static]
    needle = struct.pack("<Q", obj_base)
    for s, p1 in chase:
        checked += 1
        if checked % 5000 == 0:
            print(f"  …checked {checked}/{len(chase)} static ptrs, hits={len(one_hop)}")
        block = proc.try_read(p1, max_mid + 8)
        if not block:
            continue
        # Find obj_base anywhere in the block at an 8-byte-aligned mid we care about
        start_i = 0
        while True:
            i = block.find(needle, start_i)
            if i < 0:
                break
            if i % 8 == 0 and i in mids:
                one_hop.append((s, i))
                break
            start_i = i + 1
    if not one_hop:
        print("  (none)")
    else:
        one_hop.sort(key=lambda t: (0 if t[1] in (0x2C8, 0x270, 0x0, 0x8) else 1, t[1], t[0]))
        for s, mid in one_hop[:30]:
            print(f"  CANDIDATE: {s:X} {mid:X} 120")
            print(f"             {s:X} {mid:X} 188   # BPM")
            leaf = _follow_rkbx_pointer(proc, [s, mid], 0x120)
            bpm_a = _follow_rkbx_pointer(proc, [s, mid], 0x188)
            print(
                f"    verify sample={proc.fmt(leaf) if leaf else None} "
                f"i64={read_i64(proc, leaf) if leaf else None}  "
                f"bpm={read_f32(proc, bpm_a) if bpm_a else None}"
            )

    # --- Two hop: STATIC, HOP, MID (like 7.2.8: S 0 2C8 120)
    print("\n=== Two-hop chains: STATIC → +HOP → +MID → object ===")
    hops = [0x0, 0x8, 0x10, 0x18]
    two_mids = [0x2C8, 0x270, 0x2C0, 0x2D0, 0x300, 0x280, 0x200, 0x180, 0x100, 0x80, 0x0]
    two_max = max(two_mids)
    two_hop: list[tuple[int, int, int]] = []
    prioritized = sorted(chase, key=lambda t: abs(t[0] - 0x04D8AA18))[: args.max_static_twohop]
    for idx, (s, p1) in enumerate(prioritized):
        if idx % 2000 == 0 and idx:
            print(f"  …two-hop {idx}/{len(prioritized)}, hits={len(two_hop)}")
        for hop in hops:
            p2 = read_u64(proc, p1 + hop)
            if p2 is None or not _is_heapish(p2, proc):
                continue
            block = proc.try_read(p2, two_max + 8)
            if not block:
                continue
            start_i = 0
            while True:
                i = block.find(needle, start_i)
                if i < 0:
                    break
                if i % 8 == 0 and i in two_mids:
                    two_hop.append((s, hop, i))
                    break
                start_i = i + 1
    if not two_hop:
        print("  (none in prioritized set — try raising --max-static-twohop)")
    for s, hop, mid in two_hop[:30]:
        print(f"  CANDIDATE: {s:X} {hop:X} {mid:X} 120")
        print(f"             {s:X} {hop:X} {mid:X} 188")
        leaf = _follow_rkbx_pointer(proc, [s, hop, mid], 0x120)
        print(
            f"    verify sample={proc.fmt(leaf) if leaf else None} "
            f"i64={read_i64(proc, leaf) if leaf else None}"
        )

    print(
        "\nDone. Paste CANDIDATE / GOOD lines here.\n"
        "If none: sudo python3 …/rb_memory_scan.py trace-up "
        f"--addr 0x{sample:X} --bpm {args.bpm:g}"
    )
    return 0 if (direct or one_hop or two_hop) else 1


def cmd_ptrs(args: argparse.Namespace) -> int:
    """
    Depth-1/2 pointer scan toward sample / object base.

    Depth-2: take heap parents of the object base and find who points at them
    (static chains rarely point at the leaf object directly).
    """
    proc = MachProcess(find_rekordbox_pid())
    sample = args.addr
    # Reason: confirmed BPM at sample+0x68 ⇒ same leaf layout as 7.2.8
    # (sample @ +0x120, BPM @ +0x188).
    obj_base = sample - 0x120
    print(f"Sample:      {proc.fmt(sample)}")
    print(f"Object base: {proc.fmt(obj_base)}  (sample - 0x120)")

    print("\n=== Probe Mac 7.2.8 chain [04D8AA18, 0, 2C8] +120 ===")
    leaf = _follow_rkbx_pointer(proc, [0x04D8AA18, 0x0, 0x2C8], 0x120)
    if leaf is not None:
        v = read_i64(proc, leaf)
        print(f"  resolved sample addr {proc.fmt(leaf)}  i64={v}")
        if leaf == sample:
            print("  *** EXACT MATCH — 7.2.8 chain still valid on this build ***")
        elif v is not None:
            print("  (resolved but different address — offsets shifted)")
    else:
        print("  unreadable / shifted (use find-chain)")

    print(f"\n=== Depth-1: pointers to object base ===")
    d1 = _find_ptr_hits(proc, obj_base, args.max_hits)
    in1, heap1 = _classify_ptr_hits(proc, d1)
    print(f"  Found {len(d1)} (image={len(in1)}, heap={len(heap1)})")
    for a in in1[:40]:
        print(f"    IMAGE {proc.fmt(a)}  → object")
        print(f"      static candidate: {a - proc.base:X}")
    for a in heap1[:15]:
        print(f"    HEAP  {proc.fmt(a)}")

    if not d1:
        print("No depth-1 parents. Re-run live-triage for a fresh sample address.")
        return 1

    # Depth-2: who points at the heap parent slots? (one combined walk)
    parents = (heap1[: args.depth2_parents] if heap1 else in1[: args.depth2_parents])
    print(
        f"\n=== Depth-2: pointers to {len(parents)} depth-1 parent slots "
        f"(single memory walk) ==="
    )
    d2_map = _find_ptr_hits_multi(proc, parents, args.max_hits)
    image_hits: list[tuple[int, int]] = []
    heap_hits_show: list[tuple[int, int]] = []
    for parent in parents:
        d2 = d2_map.get(parent, [])
        in2, heap2 = _classify_ptr_hits(proc, d2)
        for a in in2:
            image_hits.append((a, parent))
        for a in heap2[:3]:
            heap_hits_show.append((a, parent))
        print(f"  parent {proc.fmt(parent)}: image={len(in2)} heap={len(heap2)}")

    if image_hits:
        print("\n*** STATIC / main-image depth-2 hits (best leads) ***")
        seen = set()
        for slot, parent in image_hits:
            if slot in seen:
                continue
            seen.add(slot)
            print(f"  {proc.fmt(slot)}  →  parent {proc.fmt(parent)}  →  object")
            print(
                f"    try offsets: {slot - proc.base:X}  "
                f"<hop?>  <mid?>  120   "
                f"# parent slot was {proc.fmt(parent)}"
            )
    else:
        print("\nNo main-image depth-2 hits yet.")
        if heap_hits_show:
            print("Sample depth-2 heap hits (may need depth-3):")
            for slot, parent in heap_hits_show[:20]:
                print(f"  {proc.fmt(slot)}  →  {proc.fmt(parent)}")

    # Depth-2b: pointers to guessed parent-structure bases
    print("\n=== Depth-2b: pointers to parent±backs (single walk) ===")
    guesses: list[int] = []
    guess_meta: dict[int, tuple[int, int]] = {}
    for parent in parents[:5]:
        for back in (0x0, 0x8, 0x10, 0x20, 0x28, 0x30, 0x38, 0x40):
            base_guess = parent - back
            guesses.append(base_guess)
            guess_meta[base_guess] = (parent, back)
    gmap = _find_ptr_hits_multi(proc, guesses, 20)
    extra_image: list[tuple[int, int, int, int]] = []
    for base_guess, hits in gmap.items():
        parent, back = guess_meta[base_guess]
        in_img, _ = _classify_ptr_hits(proc, hits)
        for a in in_img:
            extra_image.append((a, base_guess, parent, back))
    if extra_image:
        print("*** Found image pointers to parent-structure bases ***")
        seen = set()
        for a, base_guess, parent, back in extra_image[:30]:
            if a in seen:
                continue
            seen.add(a)
            print(
                f"  {proc.fmt(a)} → {proc.fmt(base_guess)} "
                f"(parent {proc.fmt(parent)} - 0x{back:X})"
            )
            print(f"    static: {a - proc.base:X}")
    else:
        print("  (none)")

    print(
        "\nDone. Paste this whole output into the chat.\n"
        "Leaf layout to keep: sample FINAL=120  BPM FINAL=188"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Rekordbox macOS memory scanner for rkbx_link offsets")
    sub = p.add_subparsers(dest="cmd", required=True)

    f = sub.add_parser("find-bpm", help="Scan for BPM float32 and optional sample neighbors")
    f.add_argument("--bpm", type=float, required=True, help="Track BPM shown in Rekordbox")
    f.add_argument("--tolerance", type=float, default=0.02)
    f.add_argument("--max-hits", type=int, default=40)
    f.add_argument("--moving", action="store_true", help="Re-check hits after a short wait")
    f.add_argument("--wait", type=float, default=2.0)
    f.add_argument("--near-samples", action="store_true", help="Search near BPM for moving u64s")
    f.add_argument("--radius", type=lambda s: int(s, 0), default=0x200)
    f.set_defaults(func=cmd_find_bpm)

    n = sub.add_parser("near", help="Dump floats/ints around an address")
    n.add_argument("--addr", type=lambda s: int(s, 0), required=True)
    n.add_argument("--radius", type=lambda s: int(s, 0), default=0x100)
    n.add_argument("--bpm", type=float, default=0.0, help="Highlight floats near this BPM")
    n.set_defaults(func=cmd_near)

    w = sub.add_parser("watch", help="Print a value repeatedly")
    w.add_argument("--addr", type=lambda s: int(s, 0), required=True)
    w.add_argument("--type", choices=("f32", "u64", "i64", "f64"), default="u64")
    w.add_argument("--interval", type=float, default=0.1)
    w.set_defaults(func=cmd_watch)

    t = sub.add_parser("triage", help="Guided play/pause test for one SAMPLE address")
    t.add_argument("--addr", type=lambda s: int(s, 0), required=True)
    t.add_argument("--prep", type=float, default=5.0, help="Seconds to play/pause before sampling")
    t.add_argument("--sample", type=float, default=2.2, help="Seconds to sample each phase")
    t.add_argument("--interval", type=float, default=0.25)
    t.set_defaults(func=cmd_triage)

    s = sub.add_parser("ptrs", help="Depth-1/2 pointer scan toward sample/object")
    s.add_argument("--addr", type=lambda s: int(s, 0), required=True)
    s.add_argument("--max-hits", type=int, default=80)
    s.add_argument(
        "--depth2-parents",
        type=int,
        default=8,
        help="How many depth-1 heap parents to scan for depth-2",
    )
    s.set_defaults(func=cmd_ptrs)

    vc = sub.add_parser("verify-chain", help="Resolve one rkbx_link offset chain")
    vc.add_argument(
        "--chain",
        required=True,
        help="Space-separated hex offsets, e.g. 'B31CC40 8 550 120'",
    )
    vc.add_argument(
        "--as-sample",
        action="store_true",
        help="Also resolve BPM at final 188",
    )
    vc.set_defaults(func=cmd_verify_chain)

    ph = sub.add_parser(
        "probe-hops",
        help="Scan deck-hop values for STATIC <hop> MID 120 with matching BPM",
    )
    ph.add_argument("--static", type=lambda s: int(s, 0), default=0xB31CC40)
    ph.add_argument("--mid", type=lambda s: int(s, 0), default=0x550)
    ph.add_argument("--bpm", type=float, required=True)
    ph.add_argument("--max-hop", type=lambda s: int(s, 0), default=0x40)
    ph.set_defaults(func=cmd_probe_hops)

    fc = sub.add_parser(
        "find-chain",
        help="Forward-search rkbx_link pointer chains from main binary to sample",
    )
    fc.add_argument("--addr", type=lambda s: int(s, 0), required=True, help="GOOD sample address")
    fc.add_argument("--bpm", type=float, default=92.0, help="Expected BPM at sample+0x68")
    fc.add_argument(
        "--image-size",
        type=lambda s: int(s, 0),
        default=48 * 1024 * 1024,
        help="Bytes of main binary to scan for static ptrs",
    )
    fc.add_argument("--max-static", type=int, default=40000, help="Max static ptrs for one-hop chase")
    fc.add_argument(
        "--max-static-twohop",
        type=int,
        default=8000,
        help="Max static ptrs (near old offset) for two-hop chase",
    )
    fc.set_defaults(func=cmd_find_chain)

    tu = sub.add_parser(
        "trace-up",
        help="Upward BFS from sample toward main binary (when find-chain fails)",
    )
    tu.add_argument("--addr", type=lambda s: int(s, 0), required=True)
    tu.add_argument("--bpm", type=float, default=92.0)
    tu.add_argument("--levels", type=int, default=4)
    tu.add_argument("--max-hits", type=int, default=200)
    tu.add_argument("--max-targets", type=int, default=80)
    tu.set_defaults(func=cmd_trace_up)

    fs = sub.add_parser(
        "find-samples",
        help="Global scan for i64 playheads advancing ~44100/s (preferred)",
    )
    fs.add_argument("--wait", type=float, default=1.0, help="Seconds between snapshots")
    fs.add_argument("--min-val", type=int, default=20_000)
    fs.add_argument("--max-val", type=int, default=500_000_000)
    fs.add_argument("--max-hits", type=int, default=40)
    fs.add_argument(
        "--max-region",
        type=int,
        default=256 * 1024 * 1024,
        help="Skip mapped regions larger than this",
    )
    fs.set_defaults(func=cmd_find_samples)

    lt = sub.add_parser(
        "live-triage",
        help="Find fresh movers then play/pause-test top candidates (one run)",
    )
    lt.add_argument("--bpm", type=float, default=92.0, help="Track BPM for next-step near hints")
    lt.add_argument("--prep", type=float, default=5.0, help="Seconds before scan / before pause sample")
    lt.add_argument("--wait", type=float, default=1.0, help="Seconds between find-samples snapshots")
    lt.add_argument("--hot-wait", type=float, default=1.0, help="Seconds for still-hot re-check")
    lt.add_argument("--sample", type=float, default=2.2, help="Seconds to sample play/pause phases")
    lt.add_argument("--interval", type=float, default=0.25)
    lt.add_argument("--pool", type=int, default=250, help="How many first-pass movers to hot-filter")
    lt.add_argument("--top", type=int, default=12, help="How many still-hot candidates to pause-test")
    lt.add_argument("--min-val", type=int, default=20_000)
    lt.add_argument("--max-val", type=int, default=500_000_000)
    lt.add_argument("--max-region", type=int, default=256 * 1024 * 1024)
    lt.set_defaults(func=cmd_live_triage)

    return p


def main(argv: Optional[list[str]] = None) -> int:
    if sys.platform != "darwin":
        print("This scanner only supports macOS.", file=sys.stderr)
        return 1
    # Reason: parse first so --help works without sudo.
    args = build_parser().parse_args(argv)
    if os.geteuid() != 0:
        print("Run with sudo (needed for task_for_pid).", file=sys.stderr)
        return 1
    try:
        return args.func(args)
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())


"""
=== FILE FLOW DOCUMENTATION ===

Functionality: Attach to Rekordbox on macOS and scan memory for BPM floats,
sample-position candidates, and depth-1 pointer parents for offset RE.

Flow:
1. Locate main rekordbox PID (exclude Agent)
2. task_for_pid + TASK_DYLD_INFO → binary base
3. find-bpm / near / watch / ptrs subcommands read live memory
4. Print absolute and base-relative addresses for offsets-macos authoring

Main Entry Point: main()

Dependencies:
- ctypes / libsystem_kernel: mach VM APIs
- Rekordbox must be resigned (get-task-allow); script must run as root
"""
