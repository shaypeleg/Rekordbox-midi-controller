"""
Test script: detect tracks loaded in Rekordbox via lsof, then print metadata,
hot cues, and waveform info.

Usage:
    python3 companion_app/test_lsof_metadata.py

Requires:
    pip install pyrekordbox
"""

import subprocess
import os
import sys
import traceback

AUDIO_EXTENSIONS = (
    ".mp3", ".m4a", ".wav", ".flac", ".aiff", ".aif",
    ".ogg", ".wma", ".alac", ".aac",
)


def is_rekordbox_running():
    """Check if Rekordbox is currently running."""
    try:
        result = subprocess.run(
            ["pgrep", "-x", "rekordbox"],
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0 and result.stdout.strip():
            return True
        result = subprocess.run(
            ["pgrep", "-f", "rekordbox.app/Contents/MacOS"],
            capture_output=True, text=True, timeout=5
        )
        return result.returncode == 0 and bool(result.stdout.strip())
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
        return True


def get_live_rekordbox_files():
    """Use lsof to find audio files currently open by Rekordbox."""
    try:
        result = subprocess.run(
            ["lsof", "-c", "rekordbox"],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode != 0:
            return []

        files = []
        seen = set()
        for line in result.stdout.splitlines():
            lower_line = line.lower()
            if not any(ext in lower_line for ext in AUDIO_EXTENSIONS):
                continue
            parts = line.split(None, 8)
            if len(parts) < 9:
                continue
            path = parts[8]
            if not path.lower().endswith(AUDIO_EXTENSIONS):
                continue
            # Filter out app-bundle resources and sampler files
            low_path = path.lower()
            if "rekordbox.app/" in low_path:
                continue
            if "/sampler/" in low_path:
                continue
            if path in seen:
                continue
            if os.path.isfile(path):
                seen.add(path)
                files.append(path)
        return files

    except (subprocess.TimeoutExpired, FileNotFoundError, OSError) as e:
        print(f"Error running lsof: {e}")
        return []


def format_ms(ms):
    """Format milliseconds as mm:ss.mmm"""
    if ms is None or ms < 0:
        return "?"
    total_s = ms / 1000.0
    minutes = int(total_s) // 60
    seconds = total_s - (minutes * 60)
    return f"{minutes}:{seconds:06.3f}"


def print_hot_cues(db, content):
    """Read and print hot cues from the Rekordbox database for this track."""
    # Kind mapping: 1-3 = A-C, (4=reserved), 5-9 = D-H
    KIND_TO_PAD = {1: 0, 2: 1, 3: 2, 5: 3, 6: 4, 7: 5, 8: 6, 9: 7}
    PAD_LETTERS = "ABCDEFGH"

    if not content.Cues:
        print("  Hot Cues: (none set)")
        return

    cues = [c for c in content.Cues if c.Kind in KIND_TO_PAD]
    if not cues:
        print("  Hot Cues: (none set)")
        return

    cues.sort(key=lambda c: KIND_TO_PAD[c.Kind])
    print(f"  Hot Cues: ({len(cues)} set)")
    for cue in cues:
        pad_idx = KIND_TO_PAD[cue.Kind]
        letter = PAD_LETTERS[pad_idx]
        time_str = format_ms(cue.InMsec) if cue.InMsec else "0:000"
        comment = f' "{cue.Comment}"' if cue.Comment else ""
        loop_str = " [LOOP]" if cue.OutMsec and cue.OutMsec > 0 else ""
        print(f"    [{letter}] {time_str}{loop_str}{comment}")


def print_waveform_info(db, content):
    """Read and print waveform availability from ANLZ files."""
    try:
        anlz_files = db.read_anlz_files(content)
        if not anlz_files:
            print("  Waveform: (no ANLZ files found)")
            return
    except Exception as e:
        print(f"  Waveform: (could not read ANLZ: {e})")
        return

    waveform_tags = []
    for ext, anlz in anlz_files.items():
        for tag in anlz.tags:
            if tag.type in ("PWAV", "PWV2", "PWV3", "PWV4", "PWV5"):
                waveform_tags.append((ext, tag.type, tag.name))

    if not waveform_tags:
        print("  Waveform: (no waveform data in ANLZ)")
        return

    print(f"  Waveform data available:")
    for ext, tag_type, tag_name in waveform_tags:
        # Get data size for context
        print(f"    [{ext}] {tag_type} ({tag_name})")


def lookup_metadata(file_paths):
    """Look up metadata, hot cues, and waveform info for each file."""
    try:
        from pyrekordbox import Rekordbox6Database
        db = Rekordbox6Database()
    except Exception as e:
        print(f"\nERROR: Could not open Rekordbox database: {e}")
        print("Make sure Rekordbox 6/7 is installed and the DB key is available.")
        print("Try running: python3 -m pyrekordbox download-key")
        return

    for i, filepath in enumerate(file_paths):
        deck_label = f"Deck {chr(65 + i)}"  # Deck A, Deck B, ...
        print(f"\n{'='*60}")
        print(f"  {deck_label}")
        print(f"{'='*60}")
        print(f"  File: {os.path.basename(filepath)}")

        try:
            content = db.get_content().filter_by(FolderPath=filepath).first()
            if content is None:
                print(f"  (not found in Rekordbox library)")
                continue

            title = content.Title or "Unknown"
            artist = "Unknown"
            if hasattr(content, "Artist") and content.Artist:
                artist = content.Artist.Name if hasattr(content.Artist, "Name") else str(content.Artist)

            album = "Unknown"
            if hasattr(content, "Album") and content.Album:
                album = content.Album.Name if hasattr(content.Album, "Name") else str(content.Album)

            genre = "Unknown"
            if hasattr(content, "Genre") and content.Genre:
                genre = content.Genre.Name if hasattr(content.Genre, "Name") else str(content.Genre)

            key = "Unknown"
            if hasattr(content, "Key") and content.Key:
                key = content.Key.ScaleName if hasattr(content.Key, "ScaleName") else str(content.Key)

            bpm = content.BPM / 100.0 if content.BPM else 0.0
            rating = content.Rating or 0
            duration_s = content.Length or 0
            duration_str = f"{duration_s // 60}:{duration_s % 60:02d}" if duration_s else "?"
            comment = content.Commnt or ""

            print(f"  Title:    {title}")
            print(f"  Artist:   {artist}")
            print(f"  Album:    {album}")
            print(f"  Genre:    {genre}")
            print(f"  Key:      {key}")
            print(f"  BPM:      {bpm:.1f}")
            print(f"  Rating:   {'*' * rating if rating else 'unrated'}")
            print(f"  Duration: {duration_str}")
            print(f"  Comment:  {comment if comment else '(empty)'}")

            print_hot_cues(db, content)
            print_waveform_info(db, content)

        except Exception as e:
            print(f"  ERROR looking up metadata: {e}")
            traceback.print_exc()


def generate_waveform_html(file_paths):
    """Generate an HTML page with waveform visualizations and hot cue markers."""
    try:
        from pyrekordbox import Rekordbox6Database
        db = Rekordbox6Database()
    except Exception as e:
        print(f"ERROR: Could not open Rekordbox database: {e}")
        return None

    decks_data = []

    for i, filepath in enumerate(file_paths):
        deck_label = f"Deck {chr(65 + i)}"
        content = db.get_content().filter_by(FolderPath=filepath).first()
        if content is None:
            continue

        title = content.Title or os.path.basename(filepath)
        artist = "Unknown"
        if hasattr(content, "Artist") and content.Artist:
            artist = content.Artist.Name if hasattr(content.Artist, "Name") else str(content.Artist)

        bpm = content.BPM / 100.0 if content.BPM else 0.0
        key = "?"
        if hasattr(content, "Key") and content.Key:
            key = content.Key.ScaleName if hasattr(content.Key, "ScaleName") else str(content.Key)
        duration_s = content.Length or 0

        # Read ANLZ files for waveform + cues
        waveform_data = None
        hot_cues = []
        try:
            anlz_files = db.read_anlz_files(content)
            if anlz_files:
                all_tags = []
                for anlz in anlz_files.values():
                    all_tags.extend(anlz.tags)

                # Pass 1: Look for PWV5 (best - color detail, clean RGB)
                for tag in all_tags:
                    if tag.type == "PWV5" and waveform_data is None:
                        try:
                            result = tag.get()
                            heights, colors = result
                            # heights normalized 0-1, colors are 3-bit (0-7)
                            # Downsample for overview
                            n = len(heights)
                            target = 1200
                            step_size = max(1, n // target)
                            ds_heights = heights[::step_size].tolist()
                            ds_colors = colors[::step_size].tolist()
                            print(f"  Using PWV5 waveform ({n} points -> {len(ds_heights)} downsampled)")
                            print(f"  Sample RGB values: {ds_colors[100]}, {ds_colors[400]}, {ds_colors[800]}")
                            waveform_data = {
                                "type": "color_detail",
                                "heights": ds_heights,
                                "colors": ds_colors,
                            }
                        except Exception as e:
                            print(f"  PWV5 parse error: {e}")

                # Pass 2: Fallback to PWV4
                if waveform_data is None:
                    for tag in all_tags:
                        if tag.type == "PWV4":
                            try:
                                heights, colors, blues = tag.get()
                                waveform_data = {
                                    "type": "color",
                                    "heights": heights.tolist(),
                                    "colors": colors.tolist(),
                                }
                                print(f"  Using PWV4 waveform ({len(heights)} points)")
                            except Exception as e:
                                print(f"  PWV4 parse error: {e}")
                            break

                # Pass 3: Fallback to PWAV mono
                if waveform_data is None:
                    for tag in all_tags:
                        if tag.type == "PWAV":
                            try:
                                result = tag.get()
                                heights = result[0] if isinstance(result, tuple) else result
                                waveform_data = {
                                    "type": "mono",
                                    "data": heights.tolist() if hasattr(heights, "tolist") else list(heights),
                                }
                                print(f"  Using PWAV mono waveform ({len(heights)} points)")
                            except Exception as e:
                                print(f"  PWAV parse error: {e}")
                            break

                # Hot cues from database (content.Cues)
                # Kind mapping: 1-3 = A-C, (4=reserved), 5-9 = D-H
                KIND_TO_PAD = {
                    1: 0, 2: 1, 3: 2,
                    5: 3, 6: 4, 7: 5, 8: 6, 9: 7,
                }
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
                if content.Cues:
                    for cue in content.Cues:
                        kind = cue.Kind
                        pad_idx = KIND_TO_PAD.get(kind)
                        if pad_idx is None:
                            continue
                        time_ms = cue.InMsec if cue.InMsec else 0
                        color = PAD_DEFAULT_COLORS[pad_idx]
                        comment = cue.Comment or ""
                        hot_cues.append({
                            "num": pad_idx + 1,
                            "letter": PAD_LETTERS[pad_idx],
                            "time_ms": time_ms,
                            "color": color,
                            "comment": comment,
                        })

        except Exception as e:
            print(f"  Warning: could not read ANLZ for {deck_label}: {e}")
            import traceback
            traceback.print_exc()

        # Deduplicate hot cues
        seen_cues = set()
        unique_cues = []
        for cue in sorted(hot_cues, key=lambda c: c["num"]):
            if cue["num"] not in seen_cues:
                seen_cues.add(cue["num"])
                unique_cues.append(cue)

        decks_data.append({
            "label": deck_label,
            "title": title,
            "artist": artist,
            "bpm": bpm,
            "key": key,
            "duration_s": duration_s,
            "waveform": waveform_data,
            "hot_cues": unique_cues,
        })

    if not decks_data:
        print("No track data to visualize.")
        return None

    import json
    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Rekordbox Waveform Viewer</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ background: #1a1a2e; color: #eee; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; padding: 20px; }}
h1 {{ color: #00d4ff; margin-bottom: 20px; font-size: 1.4em; }}
.deck {{ background: #16213e; border-radius: 12px; padding: 20px; margin-bottom: 20px; border: 1px solid #0f3460; }}
.deck-header {{ display: flex; justify-content: space-between; align-items: baseline; margin-bottom: 12px; }}
.deck-label {{ color: #00d4ff; font-size: 0.85em; font-weight: 600; text-transform: uppercase; letter-spacing: 1px; }}
.deck-meta {{ color: #888; font-size: 0.8em; }}
.track-title {{ font-size: 1.2em; font-weight: 600; margin-bottom: 4px; }}
.track-artist {{ color: #aaa; font-size: 0.95em; margin-bottom: 14px; }}
.waveform-wrap {{ position: relative; width: 100%; padding-bottom: 36px; }}
.waveform-container {{ position: relative; width: 100%; height: 70px; background: #0a0a0a; border-radius: 4px; overflow: visible; }}
canvas {{ width: 100%; height: 100%; display: block; border-radius: 4px; }}
.cue-marker {{ position: absolute; top: 0; height: 70px; width: 2px; opacity: 0.7; pointer-events: none; }}
.cue-box {{ position: absolute; top: 0; left: -7px; width: 16px; height: 14px; border-radius: 2px; display: flex; align-items: center; justify-content: center; font-size: 9px; font-weight: bold; color: #fff; text-shadow: 0 1px 1px rgba(0,0,0,0.6); }}
.cue-label-text {{ position: absolute; top: 74px; left: 50%; transform: translateX(-50%); font-size: 9px; font-weight: 600; color: #fff; white-space: nowrap; padding: 2px 5px; border-radius: 2px; }}
.cue-label-text.row-b {{ top: 90px; }}
.no-waveform {{ display: flex; align-items: center; justify-content: center; height: 80px; color: #555; font-style: italic; }}
</style>
</head>
<body>
<h1>Rekordbox Waveform Viewer</h1>
<div id="app"></div>
<script>
const decks = {json.dumps(decks_data)};

const app = document.getElementById('app');

decks.forEach((deck, idx) => {{
    const div = document.createElement('div');
    div.className = 'deck';
    const durationStr = deck.duration_s ? Math.floor(deck.duration_s/60) + ':' + String(deck.duration_s%60).padStart(2,'0') : '?';

    div.innerHTML = `
        <div class="deck-header">
            <span class="deck-label">${{deck.label}}</span>
            <span class="deck-meta">${{deck.bpm.toFixed(1)}} BPM &middot; ${{deck.key}} &middot; ${{durationStr}}</span>
        </div>
        <div class="track-title">${{deck.title}}</div>
        <div class="track-artist">${{deck.artist}}</div>
        <div class="waveform-wrap">
            <div class="waveform-container" id="wf-${{idx}}">
                ${{deck.waveform ? `<canvas id="canvas-${{idx}}"></canvas>` : '<div class="no-waveform">No waveform data available</div>'}}
            </div>
        </div>
    `;
    app.appendChild(div);

    if (deck.waveform) {{
        const canvas = document.getElementById('canvas-' + idx);
        const container = document.getElementById('wf-' + idx);
        const rect = container.getBoundingClientRect();
        canvas.width = rect.width * 2;
        canvas.height = rect.height * 2;
        canvas.style.width = rect.width + 'px';
        canvas.style.height = rect.height + 'px';
        const ctx = canvas.getContext('2d');
        ctx.scale(2, 2);
        const w = rect.width;
        const h = rect.height;
        const midY = h / 2;

        if (deck.waveform.type === 'color_detail') {{
            // PWV5: heights normalized 0-1, colors are 3-bit RGB (0-7)
            const heights = deck.waveform.heights;
            const colors = deck.waveform.colors;
            const numEntries = heights.length;
            const step = w / numEntries;
            const barWidth = Math.max(0.5, step * 0.6);

            for (let i = 0; i < numEntries; i++) {{
                const x = i * step;
                const barH = heights[i] * midY * 0.92;
                if (barH < 0.3) continue;

                const r = Math.min(255, Math.round((colors[i][0] / 7) * 255));
                const g = Math.min(255, Math.round((colors[i][1] / 7) * 255));
                const b = Math.min(255, Math.round((colors[i][2] / 7) * 255));

                ctx.fillStyle = `rgb(${{r}},${{g}},${{b}})`;
                ctx.fillRect(x, midY - barH, barWidth, barH * 2);
            }}

            ctx.strokeStyle = 'rgba(255,255,255,0.08)';
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(0, midY);
            ctx.lineTo(w, midY);
            ctx.stroke();

        }} else if (deck.waveform.type === 'color') {{
            // PWV4 fallback
            const heights = deck.waveform.heights;
            const colors = deck.waveform.colors;
            const numEntries = heights.length;
            const step = w / numEntries;
            const barWidth = Math.max(0.5, step * 0.6);

            let maxH = 1;
            for (let i = 0; i < numEntries; i++) {{
                const h = Array.isArray(heights[i]) ? Math.max(...heights[i]) : heights[i];
                if (h > maxH) maxH = h;
            }}

            for (let i = 0; i < numEntries; i++) {{
                const x = i * step;
                const h = Array.isArray(heights[i]) ? heights[i] : [heights[i], heights[i]];
                const frontH = (h[0] / maxH) * midY * 0.92;

                let r = 0, g = 180, b = 255;
                if (colors[i]) {{
                    const layer = Array.isArray(colors[i][0]) ? colors[i][1] || colors[i][0] : colors[i];
                    r = Math.min(255, Math.round(layer[0] * 1.7));
                    g = Math.min(255, Math.round(layer[1] * 1.7));
                    b = Math.min(255, Math.round(layer[2] * 1.7));
                }}

                if (frontH > 0) {{
                    ctx.fillStyle = `rgb(${{r}},${{g}},${{b}})`;
                    ctx.fillRect(x, midY - frontH, barWidth, frontH * 2);
                }}
            }}

            ctx.strokeStyle = 'rgba(255,255,255,0.08)';
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(0, midY);
            ctx.lineTo(w, midY);
            ctx.stroke();

        }} else {{
            // Mono waveform (PWAV - 400 points)
            const data = deck.waveform.data;
            const numEntries = data.length;
            const step = w / numEntries;
            let maxVal = 1;
            for (let i = 0; i < numEntries; i++) {{
                const val = Array.isArray(data[i]) ? data[i][0] : data[i];
                if (val > maxVal) maxVal = val;
            }}
            for (let i = 0; i < numEntries; i++) {{
                const val = Array.isArray(data[i]) ? data[i][0] : data[i];
                const barH = (val / maxVal) * midY * 0.9;
                ctx.fillStyle = `rgb(0, ${{Math.floor(100 + (val/maxVal)*155)}}, 255)`;
                ctx.fillRect(i * step, midY - barH, Math.ceil(step), barH * 2);
            }}
            ctx.strokeStyle = 'rgba(255,255,255,0.15)';
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(0, midY);
            ctx.lineTo(w, midY);
            ctx.stroke();
        }}

        // Draw hot cue markers on the waveform (Rekordbox-style boxes with labels)
        if (deck.duration_s > 0) {{
            const sortedCues = [...deck.hot_cues].sort((a,b) => a.time_ms - b.time_ms);
            sortedCues.forEach((cue, cueIdx) => {{
                const xPos = (cue.time_ms / (deck.duration_s * 1000)) * w;
                const marker = document.createElement('div');
                marker.className = 'cue-marker';
                marker.style.left = xPos + 'px';
                marker.style.background = cue.color;
                // Colored box with letter at top
                const box = document.createElement('div');
                box.className = 'cue-box';
                box.style.background = cue.color;
                box.textContent = cue.letter;
                marker.appendChild(box);
                // Label below waveform, alternating rows
                const label = document.createElement('div');
                const timeStr = Math.floor(cue.time_ms/1000/60) + ':' + String(Math.floor(cue.time_ms/1000)%60).padStart(2,'0');
                const labelText = cue.comment ? cue.comment + ' ' + timeStr : timeStr;
                label.className = 'cue-label-text' + (cueIdx % 2 === 1 ? ' row-b' : '');
                label.style.background = cue.color;
                label.textContent = labelText;
                marker.appendChild(label);
                container.appendChild(marker);
            }});
        }}
    }}
}});
</script>
</body>
</html>"""
    return html


def main():
    print("Rekordbox Track Metadata Test")
    print("-" * 40)

    if not is_rekordbox_running():
        print("\nRekordbox is NOT running. Start it and load some tracks first.")
        sys.exit(1)

    print("\nRekordbox is running. Checking open audio files via lsof...")
    files = get_live_rekordbox_files()

    if not files:
        print("\nNo audio files detected as open by Rekordbox.")
        print("Make sure you have tracks loaded on a deck.")
        sys.exit(1)

    print(f"\nFound {len(files)} open track file(s):")
    for f in files:
        print(f"  - {os.path.basename(f)}")

    print("\nLooking up metadata from Rekordbox library...")
    lookup_metadata(files)

    print("\n\nGenerating waveform HTML viewer...")
    html = generate_waveform_html(files)
    if html:
        output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "waveform_viewer.html")
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(html)
        print(f"  Saved to: {output_path}")
        print(f"  Opening in browser...")
        import webbrowser
        webbrowser.open(f"file://{output_path}")
    print()


if __name__ == "__main__":
    main()
