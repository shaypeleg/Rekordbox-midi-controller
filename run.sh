#!/usr/bin/env bash
# Start the CYD companion stack:
#   1) rkbx_link  -- live Rekordbox playhead over OSC (GPL-3.0 upstream)
#   2) nowplaying_server.py -- Track Info WebSocket + crossfader relay
#
# Usage:
#   ./run.sh              normal
#   ./run.sh -d           + DDJ-REV5 MIDI diagnostic
#   ./run.sh -h           help
#   ./run.sh --no-rkbx    skip rkbx_link (static waveform only)
#   ./run.sh -resign      one-time macOS: re-sign Rekordbox for memory access
#   ./run.sh -stop        kill background rkbx_link / BeatKeeper processes
#
# macOS: live playhead needs Rekordbox re-signed once (`./run.sh -resign`),
# then rkbx_link is typically launched with sudo. See README.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
COMPANION="$ROOT/companion_app"
ENSURE="$COMPANION/scripts/ensure_rkbx_link.sh"
RKBX_LOG="${COMPANION}/.vendor/rkbx_link/rkbx_link.log"

RKBX_PID=""
NO_RKBX=0
DO_RESIGN=0
DO_STOP=0
SHOW_HELP=0
SERVER_ARGS=()

for arg in "$@"; do
  case "$arg" in
    --no-rkbx) NO_RKBX=1 ;;
    -resign|--resign) DO_RESIGN=1 ;;
    -stop|--stop) DO_STOP=1 ;;
    -h|-help|--help) SHOW_HELP=1; SERVER_ARGS+=("-h") ;;
    *) SERVER_ARGS+=("$arg") ;;
  esac
done

# Kill all rkbx_link instances (including sudo/root children). Safe to call
# when nothing is running.
stop_rkbx_link() {
  echo "[run.sh] Stopping rkbx_link / BeatKeeper..."
  # Prefer sudo so root-owned processes started via sudo are killed too.
  if command -v sudo >/dev/null 2>&1; then
    sudo -n killall rkbx_link 2>/dev/null || true
    sudo killall rkbx_link 2>/dev/null || true
    sudo -n pkill -f '/\.vendor/rkbx_link/rkbx_link' 2>/dev/null || true
    sudo pkill -f '/\.vendor/rkbx_link/rkbx_link' 2>/dev/null || true
  fi
  killall rkbx_link 2>/dev/null || true
  pkill -f '/\.vendor/rkbx_link/rkbx_link' 2>/dev/null || true

  if pgrep -f '/\.vendor/rkbx_link/rkbx_link' >/dev/null 2>&1; then
    echo "[run.sh] WARNING: rkbx_link still running — try: sudo killall rkbx_link"
    pgrep -lf rkbx_link || true
    return 1
  fi
  echo "[run.sh] rkbx_link stopped."
  return 0
}

cleanup() {
  # Always stop by name: RKBX_PID is often just the sudo wrapper / subshell,
  # while BeatKeeper runs as root and survives a normal kill.
  stop_rkbx_link >/dev/null 2>&1 || true
  if [[ -n "${RKBX_PID}" ]]; then
    wait "${RKBX_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

ensure_vendor() {
  if [[ ! -x "${ENSURE}" ]]; then
    chmod +x "${ENSURE}"
  fi
  "${ENSURE}"
}

# Return installed Rekordbox short version (e.g. 7.2.16) or empty.
detect_rekordbox_version() {
  local app="/Applications/rekordbox 7/rekordbox.app"
  if [[ ! -d "${app}" ]]; then
    app="/Applications/rekordbox.app"
  fi
  if [[ ! -d "${app}" ]]; then
    return 0
  fi
  mdls -name kMDItemVersion "${app}" 2>/dev/null \
    | sed -n 's/.*"\([0-9][0-9.]*\)".*/\1/p' \
    | head -1
}

# macOS community offsets only ship for 7.2.8 — warn loudly on mismatch.
warn_if_rekordbox_unsupported() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return 0
  fi
  local ver
  ver="$(detect_rekordbox_version)"
  [[ -z "${ver}" ]] && return 0

  # Compare major.minor.patch prefix (ignore build suffix like .0342)
  local short
  short="$(echo "${ver}" | awk -F. '{print $1"."$2"."$3}')"
  local supported="7.2.8"
  if [[ "${short}" != "${supported}" ]]; then
    cat >&2 <<EOF
[run.sh] ************************************************************
[run.sh] WARNING: Rekordbox ${ver} detected.
[run.sh] macOS rkbx_link offsets currently only support ${supported}.
[run.sh] Live playhead will NOT work (memory read fails → no OSC /time).
[run.sh] Track Info waveform/cues still work; needle stays offline.
[run.sh]
[run.sh] Options:
[run.sh]   1) Install Rekordbox ${supported} (only version with Mac offsets)
[run.sh]   2) Watch https://github.com/grufkork/rkbx_link/issues/55 for
[run.sh]      newer macOS offsets
[run.sh] ************************************************************
EOF
  else
    echo "[run.sh] Rekordbox ${ver} matches macOS rkbx_link offsets (${supported})."
  fi
}

resign_rekordbox() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "[run.sh] -resign is only needed on macOS (memory entitlements)."
    exit 1
  fi

  echo "[run.sh] Ensuring rkbx_link vendor tree..."
  local workdir
  workdir="$(ensure_vendor)"
  local script="${workdir}/resign_rekordbox.sh"
  if [[ ! -f "${script}" ]]; then
    echo "[run.sh] ERROR: resign script not found at ${script}"
    exit 1
  fi
  chmod +x "${script}"

  echo "[run.sh] Running Rekordbox re-sign (adds get-task-allow for live playhead)..."
  echo "[run.sh] Quit Rekordbox first if it is open."
  (cd "${workdir}" && bash "${script}")
  echo ""
  echo "[run.sh] Re-sign finished. Start Rekordbox (right-click -> Open if macOS warns),"
  echo "[run.sh] then run: ./run.sh"
}

start_rkbx_link() {
  # Clear any leftover BeatKeeper from a previous crash / Ctrl+C
  stop_rkbx_link >/dev/null 2>&1 || true

  warn_if_rekordbox_unsupported

  local workdir
  workdir="$(ensure_vendor)"
  local bin="${workdir}/rkbx_link"
  if [[ ! -x "${bin}" ]]; then
    echo "[run.sh] rkbx_link binary not found in ${workdir} -- continuing without live playhead"
    return 0
  fi

  mkdir -p "$(dirname "${RKBX_LOG}")"
  : > "${RKBX_LOG}"

  local cmd=("${bin}")
  if [[ "$(uname -s)" == "Darwin" ]]; then
    if sudo -n true 2>/dev/null; then
      cmd=(sudo -n "${bin}")
    else
      echo "[run.sh] rkbx_link on macOS typically needs sudo (memory access)."
      echo "[run.sh] You may be prompted for your password."
      echo "[run.sh] One-time setup: ./run.sh -resign"
      cmd=(sudo "${bin}")
    fi
  fi

  echo "[run.sh] Starting rkbx_link in ${workdir}..."
  echo "[run.sh] Logs: ${RKBX_LOG}  (stop with Ctrl+C or ./run.sh -stop)"
  (
    cd "${workdir}"
    # Reason: keep BeatKeeper spam out of the interactive terminal; it was
    # also orphaning writes after run.sh exited because sudo children survived.
    "${cmd[@]}" >>"${RKBX_LOG}" 2>&1
  ) &
  RKBX_PID=$!
  sleep 1
  if ! pgrep -f '/\.vendor/rkbx_link/rkbx_link' >/dev/null 2>&1; then
    echo "[run.sh] WARNING: rkbx_link exited immediately -- live playhead unavailable"
    echo "[run.sh] Last log lines:"
    tail -n 20 "${RKBX_LOG}" 2>/dev/null || true
    echo "[run.sh] Check Rekordbox is running with a track loaded, version matches config,"
    echo "[run.sh] and you ran ./run.sh -resign on macOS."
    RKBX_PID=""
  else
    echo "[run.sh] rkbx_link running -> OSC 127.0.0.1:4460"
  fi
}

if [[ "${DO_STOP}" -eq 1 ]]; then
  # Disable EXIT trap kill-twice noise; we are only stopping.
  trap - EXIT INT TERM
  stop_rkbx_link
  exit $?
fi

if [[ "${DO_RESIGN}" -eq 1 ]]; then
  trap - EXIT INT TERM
  resign_rekordbox
  exit 0
fi

# Help only -- do not start rkbx_link / vendor update
if [[ "${SHOW_HELP}" -eq 1 ]]; then
  trap - EXIT INT TERM
  cat <<'EOF'
CYD companion launcher

Usage:
  ./run.sh              Start rkbx_link + Track Info server + crossfader relay
  ./run.sh -d           Same, plus DDJ-REV5 MIDI diagnostic
  ./run.sh --no-rkbx    Skip rkbx_link (static waveform only)
  ./run.sh -resign      macOS one-time: re-sign Rekordbox for live playhead
  ./run.sh -stop        Kill background rkbx_link / BeatKeeper
  ./run.sh -h           Show this help

EOF
  cd "${COMPANION}"
  pip install -q -r requirements.txt
  python3 nowplaying_server.py -h
  exit 0
fi

cd "${COMPANION}"
pip install -q -r requirements.txt

if [[ "${NO_RKBX}" -eq 0 ]]; then
  start_rkbx_link || echo "[run.sh] rkbx_link setup failed -- continuing without live playhead"
else
  echo "[run.sh] --no-rkbx: skipping live playhead"
fi

echo "[run.sh] Starting Now-Playing server..."
# With set -u, an empty array expansion ("${arr[@]}") is an unbound-variable error.
if [[ ${#SERVER_ARGS[@]} -gt 0 ]]; then
  python3 nowplaying_server.py "${SERVER_ARGS[@]}"
else
  python3 nowplaying_server.py
fi
