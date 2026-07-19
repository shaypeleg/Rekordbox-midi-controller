#!/usr/bin/env bash
# Ensure rkbx_link is available for the companion app.
#
# - Clones upstream source (GPL-3.0) into companion_app/.vendor/rkbx_link
# - Prefers the macOS/Windows release binary (no Rust toolchain required)
# - Falls back to `cargo build --release` when the binary download fails
# - Writes a CYD-tuned config (OSC playhead → 127.0.0.1:4460)
#
# Prints the absolute path to the rkbx_link working directory on stdout
# (run the binary from that directory so ./config and ./data resolve).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPANION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR_DIR="$COMPANION_DIR/.vendor"
SRC_DIR="$VENDOR_DIR/rkbx_link"
REPO_URL="https://github.com/grufkork/rkbx_link.git"
RELEASE_TAG="${RKBX_LINK_TAG:-v1.2.0}"

log() { echo "[rkbx_link] $*" >&2; }

detect_platform() {
  case "$(uname -s)" in
    Darwin) echo "macos" ;;
    Linux)  echo "linux" ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT) echo "win" ;;
    *) echo "unknown" ;;
  esac
}

write_cyd_config() {
  local dest="$1"
  local rb_version="${RKBX_REKORDBOX_VERSION:-7.2.8}"
  cat > "$dest" <<EOF
# Auto-generated for Rekordbox-Midi-Controller CYD companion.
# Source: https://github.com/grufkork/rkbx_link (GPL-3.0)
# Override Rekordbox version with RKBX_REKORDBOX_VERSION=x.y.z

app.licensekey evaluation
app.auto_update false
app.debug false
app.yes_to_all true

keeper.rekordbox_version ${rb_version}
keeper.update_rate 60
keeper.slow_update_every_nth 10
keeper.delay_compensation 0
keeper.keep_warm true
keeper.decks 2

display.enabled false

# Ableton Link off — companion only needs OSC playhead
link.enabled false

osc.enabled true
osc.source 127.0.0.1:4450
osc.destination 127.0.0.1:4460
osc.send_every_nth 2
osc.phrase_output_format int
osc.trigger_autorelease false

# Per-deck playhead (seconds) — required for CYD needle
osc.msg.n/time true
osc.msg.master/time false
osc.msg.n/phrase false
osc.msg.master/phrase false
osc.msg.master/beat/subdiv
osc.msg.master/beat/trigger
osc.msg.n/beat/subdiv
osc.msg.n/beat/trigger

file.enabled false
setlist.enabled false
sacn.enabled false
EOF
}

clone_or_update_source() {
  mkdir -p "$VENDOR_DIR"
  if [[ -d "$SRC_DIR/.git" ]]; then
    log "Updating source in $SRC_DIR"
    git -C "$SRC_DIR" fetch --depth 1 origin "$RELEASE_TAG" >/dev/null 2>&1 \
      || git -C "$SRC_DIR" fetch --depth 1 origin master >/dev/null 2>&1 || true
    git -C "$SRC_DIR" checkout -q FETCH_HEAD 2>/dev/null \
      || git -C "$SRC_DIR" pull -q --ff-only || true
  else
    log "Cloning $REPO_URL ($RELEASE_TAG)..."
    rm -rf "$SRC_DIR"
    if ! git clone --depth 1 --branch "$RELEASE_TAG" "$REPO_URL" "$SRC_DIR" 2>/dev/null; then
      git clone --depth 1 "$REPO_URL" "$SRC_DIR"
    fi
  fi
}

download_release_binary() {
  local platform="$1"
  local zip_name bin_name url
  case "$platform" in
    macos) zip_name="rkbx_link_macos.zip"; bin_name="rkbx_link" ;;
    win)   zip_name="rkbx_link_win.zip";   bin_name="rkbx_link.exe" ;;
    *)     return 1 ;;
  esac
  url="https://github.com/grufkork/rkbx_link/releases/download/${RELEASE_TAG}/${zip_name}"
  local tmp
  tmp="$(mktemp -d)"
  log "Downloading release binary ${RELEASE_TAG}/${zip_name}..."
  if ! curl -fsSL -o "$tmp/rkbx.zip" "$url"; then
    rm -rf "$tmp"
    return 1
  fi
  unzip -qo "$tmp/rkbx.zip" -d "$tmp/out"
  if [[ ! -f "$tmp/out/$bin_name" ]]; then
    rm -rf "$tmp"
    return 1
  fi
  cp "$tmp/out/$bin_name" "$SRC_DIR/$bin_name"
  chmod +x "$SRC_DIR/$bin_name"
  # Prefer release data/offsets if present
  if [[ -d "$tmp/out/data" ]]; then
    mkdir -p "$SRC_DIR/data"
    cp -R "$tmp/out/data/." "$SRC_DIR/data/"
  fi
  rm -rf "$tmp"
  log "Installed binary → $SRC_DIR/$bin_name"
  return 0
}

build_from_source() {
  if ! command -v cargo >/dev/null 2>&1; then
    return 1
  fi
  log "Building rkbx_link from source (cargo release)..."
  (cd "$SRC_DIR" && cargo build --release)
  local built="$SRC_DIR/target/release/rkbx_link"
  if [[ -f "$built" ]]; then
    cp "$built" "$SRC_DIR/rkbx_link"
    chmod +x "$SRC_DIR/rkbx_link"
    return 0
  fi
  return 1
}

main() {
  local platform
  platform="$(detect_platform)"
  clone_or_update_source
  write_cyd_config "$SRC_DIR/config"

  local bin="$SRC_DIR/rkbx_link"
  if [[ "$platform" == "win" ]]; then
    bin="$SRC_DIR/rkbx_link.exe"
  fi

  if [[ ! -x "$bin" ]]; then
    if ! download_release_binary "$platform"; then
      if ! build_from_source; then
        log "ERROR: could not obtain rkbx_link binary."
        log "Install Rust (https://rustup.rs) or set RKBX_LINK_BIN to an existing binary."
        exit 1
      fi
    fi
  else
    log "Using existing binary $bin"
  fi

  if [[ ! -x "$bin" && -n "${RKBX_LINK_BIN:-}" && -x "${RKBX_LINK_BIN}" ]]; then
    cp "${RKBX_LINK_BIN}" "$bin"
    chmod +x "$bin"
  fi

  if [[ ! -x "$bin" ]]; then
    log "ERROR: rkbx_link binary missing at $bin"
    exit 1
  fi

  # Absolute path to the working directory (stdout — consumed by run.sh)
  echo "$SRC_DIR"
}

main "$@"
