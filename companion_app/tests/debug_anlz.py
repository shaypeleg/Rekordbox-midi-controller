"""Debug script: inspect what ANLZ data is available for loaded tracks."""

import subprocess
import os
import sys

AUDIO_EXTENSIONS = (
    ".mp3", ".m4a", ".wav", ".flac", ".aiff", ".aif",
    ".ogg", ".wma", ".alac", ".aac",
)


def is_rekordbox_running():
    try:
        result = subprocess.run(
            ["pgrep", "-f", "rekordbox"],
            capture_output=True, text=True, timeout=5
        )
        return result.returncode == 0 and bool(result.stdout.strip())
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
        return True


def get_live_rekordbox_files():
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


def main():
    from pyrekordbox import Rekordbox6Database

    if not is_rekordbox_running():
        print("Rekordbox is not running.")
        sys.exit(1)

    files = get_live_rekordbox_files()
    if not files:
        print("No audio files detected.")
        sys.exit(1)

    db = Rekordbox6Database()

    for filepath in files:
        print(f"\n{'='*60}")
        print(f"File: {os.path.basename(filepath)}")
        print(f"{'='*60}")

        content = db.get_content().filter_by(FolderPath=filepath).first()
        if content is None:
            print("  NOT FOUND in Rekordbox library")
            continue

        print(f"  Title: {content.Title}")
        print(f"  AnalysisDataPath: {content.AnalysisDataPath}")

        # Try to get ANLZ paths
        try:
            paths = db.get_anlz_paths(content)
            print(f"  ANLZ paths: {paths}")
        except Exception as e:
            print(f"  get_anlz_paths ERROR: {e}")

        # Try to read ANLZ files
        try:
            anlz_files = db.read_anlz_files(content)
            if not anlz_files:
                print("  read_anlz_files returned empty/None")
                continue

            print(f"  ANLZ files loaded: {list(anlz_files.keys())}")

            for ext, anlz in anlz_files.items():
                print(f"\n  --- {ext} file ---")
                print(f"  All tags: {[t.type for t in anlz.tags]}")
                for tag in anlz.tags:
                    print(f"    Tag: type={tag.type}, name={tag.name}")
                    # Try to get waveform data size
                    if tag.type in ("PWAV", "PWV2", "PWV3", "PWV4", "PWV5"):
                        try:
                            data = tag.get()
                            if isinstance(data, tuple):
                                print(f"      -> tuple of {len(data)} items, first shape: {data[0].shape if hasattr(data[0], 'shape') else len(data[0])}")
                            elif hasattr(data, 'shape'):
                                print(f"      -> shape: {data.shape}")
                            else:
                                print(f"      -> type: {type(data)}, len: {len(data) if hasattr(data, '__len__') else '?'}")
                        except Exception as e:
                            print(f"      -> ERROR getting data: {e}")
                    elif "cue" in tag.name:
                        try:
                            if hasattr(tag.content, "type"):
                                print(f"      -> cue type: {tag.content.type} (0=memory, 1=hot cue)")
                            if hasattr(tag.content, "entries"):
                                print(f"      -> {len(tag.content.entries)} entries")
                                for entry in tag.content.entries[:4]:
                                    attrs = {k: getattr(entry, k) for k in ["hot_cue", "time", "loop_time", "color_red", "color_green", "color_blue"] if hasattr(entry, k)}
                                    print(f"         {attrs}")
                        except Exception as e:
                            print(f"      -> ERROR: {e}")

        except Exception as e:
            print(f"  read_anlz_files ERROR: {e}")
            import traceback
            traceback.print_exc()


if __name__ == "__main__":
    main()
