#!/usr/bin/env python3
"""
Ace Combat Zero: The Belkan War — Visual 2D/3D Map Editor (Launcher)
====================================================================
Starts the local Python API server and opens the Three.js WebGL2 Map Editor
in your default web browser.

Usage:
    python py/ACZ_Map_Editor.py
    python py/ACZ_Map_Editor.py --map m02a --port 8765
    python py/ACZ_Map_Editor.py --no-browser
"""

from __future__ import annotations

import argparse
import sys
import threading
import webbrowser
from pathlib import Path

ROOT_PY = Path(__file__).resolve().parent
if str(ROOT_PY) not in sys.path:
    sys.path.insert(0, str(ROOT_PY))

from map_editor.server import run_server


def auto_detect_game_dir() -> Path:
    # Default layout: <GameDir>/ACZero Enhancer/py/ACZ_Map_Editor.py
    candidates = [
        ROOT_PY.parent.parent,
        ROOT_PY.parent,
        Path.cwd().parent,
        Path.cwd(),
    ]
    for c in candidates:
        if (c / "extracted" / "a" / "data" / "map").is_dir():
            return c
    return ROOT_PY.parent.parent


def main() -> int:
    game_dir = auto_detect_game_dir()
    parser = argparse.ArgumentParser(description="ACZero Visual 2D/3D Map Editor")
    parser.add_argument(
        "--maps-root",
        type=Path,
        default=game_dir / "extracted" / "a" / "data" / "map",
        help="Path to extracted/a/data/map directory",
    )
    parser.add_argument(
        "--hd-terrain",
        type=Path,
        default=game_dir / "mods" / "textures" / "terrain",
        help="Path to mods/textures/terrain HD tiles directory",
    )
    parser.add_argument(
        "--overrides-root",
        type=Path,
        default=game_dir / "mods" / "maps",
        help="Output directory for non-destructive map edits (mods/maps/<map>/)",
    )
    parser.add_argument("--host", default="127.0.0.1", help="Bind host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="Bind port (default: 8765)")
    parser.add_argument("--map", dest="initial_map", default="m02a", help="Initial map to open (default: m02a)")
    parser.add_argument("--no-browser", action="store_true", help="Do not automatically open browser")
    args = parser.parse_args()

    print("============================================================")
    print("  Ace Combat Zero — Visual 2D/3D Map Editor (Phase 1 & 2)")
    print("============================================================")
    print(f"  Maps root      : {args.maps_root}")
    print(f"  HD terrain dir : {args.hd_terrain}")
    print(f"  Overrides dir  : {args.overrides_root}")

    server = run_server(
        maps_root=args.maps_root,
        hd_terrain_dir=args.hd_terrain,
        overrides_root=args.overrides_root,
        host=args.host,
        port=args.port,
    )
    url = f"http://{args.host}:{args.port}/"
    if args.initial_map:
        url += f"?map={args.initial_map}"
    print(f"  Editor URL     : {url}")
    print("  Press Ctrl+C to stop.")
    print("============================================================")

    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down Map Editor server...")
        server.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
