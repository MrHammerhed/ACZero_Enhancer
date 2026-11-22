#!/usr/bin/env python3
"""
Ace Combat Zero Visual Map Editor — Local HTTP API Server
=========================================================
Zero external web-framework dependencies (uses Python stdlib ThreadingHTTPServer).
Serves static frontend assets from py/map_editor/web/ and REST/Binary endpoints for map editing.
"""

from __future__ import annotations

import base64
import io
import json
import mimetypes
import shutil
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, Optional, Tuple
from urllib.parse import parse_qs, urlparse

import numpy as np

from . import map_io

WEB_DIR = Path(__file__).resolve().parent / "web"


class EditorContext:
    def __init__(self, maps_root: Path, hd_terrain_dir: Path, overrides_root: Path):
        self.maps_root = maps_root
        self.hd_terrain_dir = hd_terrain_dir
        self.overrides_root = overrides_root
        self._lock = threading.Lock()
        self._bundle_cache: Dict[str, map_io.MapBundle] = {}
        self._terrain_png_cache: Dict[Tuple[str, bool, int], bytes] = {}
        self._atlas_cache: Dict[Tuple[str, bool, int], Tuple[bytes, int, int, int, bytes]] = {}

    def list_maps(self) -> list:
        if not self.maps_root.is_dir():
            return []
        result = []
        for d in sorted(self.maps_root.iterdir()):
            if not d.is_dir():
                continue
            name = d.name
            if not ((d / f"{name}.mha").is_file() and (d / f"{name}.mhd").is_file()):
                continue
            ovr_dir = self.overrides_root / name
            has_ovr = any((ovr_dir / f"{name}.{ext}").is_file() for ext in ("pdl", "pml", "pmi", "efa"))
            pdl_path, _ = map_io.resolve_file(d, ovr_dir, f"{name}.pdl")
            obj_cnt = 0
            if pdl_path.is_file():
                b = pdl_path.read_bytes()
                if len(b) >= 4:
                    obj_cnt = int.from_bytes(b[:4], "little")
            pmmdl_path = d / f"pmmdl{name}.bin"
            mdl_cnt = 0
            if pmmdl_path.is_file():
                b = pmmdl_path.read_bytes()
                if len(b) >= 4:
                    mdl_cnt = int.from_bytes(b[:4], "little")
            result.append({
                "name": name,
                "has_overrides": has_ovr,
                "object_count": obj_cnt,
                "model_count": mdl_cnt,
            })
        return result

    def get_bundle(self, map_name: str, force_reload: bool = False) -> Optional[map_io.MapBundle]:
        vanilla_dir = self.maps_root / map_name
        if not vanilla_dir.is_dir():
            return None
        with self._lock:
            if not force_reload and map_name in self._bundle_cache:
                return self._bundle_cache[map_name]
        bundle = map_io.load_map_bundle(vanilla_dir, self.overrides_root / map_name, map_name)
        with self._lock:
            self._bundle_cache[map_name] = bundle
        return bundle

    def get_terrain_png(self, map_name: str, use_hd: bool, size: int) -> Optional[bytes]:
        size = max(512, min(4096, size))
        key = (map_name, use_hd, size)
        with self._lock:
            if key in self._terrain_png_cache:
                return self._terrain_png_cache[key]
        vanilla_dir = self.maps_root / map_name
        if not vanilla_dir.is_dir():
            return None
        img = map_io.render_terrain_overview(
            vanilla_dir,
            map_name,
            hd_terrain_dir=self.hd_terrain_dir,
            use_hd=use_hd,
            out_size=size,
        )
        if img is None:
            return None
        buf = io.BytesIO()
        img.save(buf, format="PNG", optimize=False)
        data = buf.getvalue()
        with self._lock:
            # Keep cache bounded to ~6 images
            if len(self._terrain_png_cache) > 6:
                self._terrain_png_cache.clear()
            self._terrain_png_cache[key] = data
        return data

    def get_terrain_atlas(
        self, map_name: str, use_hd: bool, tile_px: int = 128
    ) -> Optional[Tuple[bytes, int, int, int, bytes]]:
        tile_px = max(32, min(256, tile_px))
        key = (map_name, use_hd, tile_px)
        with self._lock:
            if key in self._atlas_cache:
                return self._atlas_cache[key]
        vanilla_dir = self.maps_root / map_name
        if not vanilla_dir.is_dir():
            return None
        res = map_io.build_terrain_atlas_and_tilemap(
            vanilla_dir,
            map_name,
            hd_terrain_dir=self.hd_terrain_dir,
            use_hd=use_hd,
            tile_px=tile_px,
        )
        if res is None:
            return None
        atlas_img, cols, tile_count, actual_tile_px, tilemap_u16 = res
        buf = io.BytesIO()
        atlas_img.save(buf, format="JPEG", quality=92, subsampling=0)
        entry = (buf.getvalue(), cols, tile_count, actual_tile_px, tilemap_u16.tobytes())
        with self._lock:
            if len(self._atlas_cache) > 4:
                self._atlas_cache.clear()
            self._atlas_cache[key] = entry
        return entry

    def save_map(self, map_name: str, objects: list, efa_bytes: bytes) -> dict:
        bundle = self.get_bundle(map_name)
        models = bundle.models if bundle else None
        efa = map_io.parse_efa(efa_bytes)
        ovr_dir = self.overrides_root / map_name
        summary = map_io.save_map_overrides(ovr_dir, map_name, objects, efa, models=models)
        # Refresh cached bundle
        self.get_bundle(map_name, force_reload=True)
        return summary

    def revert_map(self, map_name: str) -> dict:
        ovr_dir = self.overrides_root / map_name
        removed = []
        if ovr_dir.is_dir():
            for ext in ("pdl", "pml", "pmi", "efa"):
                p = ovr_dir / f"{map_name}.{ext}"
                if p.is_file():
                    p.unlink()
                    removed.append(str(p))
            try:
                if not any(ovr_dir.iterdir()):
                    ovr_dir.rmdir()
            except OSError:
                pass
        self.get_bundle(map_name, force_reload=True)
        return {"map_name": map_name, "removed": removed}


def make_handler(ctx: EditorContext):
    class MapEditorHandler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args) -> None:
            # Concise server log
            pass

        def _send_bytes(self, status: int, content_type: str, data: bytes, extra_headers: Optional[dict] = None):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            if extra_headers:
                for k, v in extra_headers.items():
                    self.send_header(k, str(v))
            self.end_headers()
            self.wfile.write(data)

        def _send_json(self, obj, status: int = HTTPStatus.OK):
            payload = json.dumps(obj, separators=(",", ":")).encode("utf-8")
            self._send_bytes(status, "application/json; charset=utf-8", payload)

        def _send_error_json(self, status: int, msg: str):
            self._send_json({"error": msg}, status=status)

        def _serve_static(self, rel_path: str):
            if rel_path in ("", "/"):
                rel_path = "index.html"
            rel_path = rel_path.lstrip("/")
            full = (WEB_DIR / rel_path).resolve()
            if not str(full).startswith(str(WEB_DIR.resolve())) or not full.is_file():
                self._send_error_json(HTTPStatus.NOT_FOUND, f"Static file not found: {rel_path}")
                return
            ctype, _ = mimetypes.guess_type(str(full))
            if full.suffix == ".js":
                ctype = "application/javascript; charset=utf-8"
            elif full.suffix == ".css":
                ctype = "text/css; charset=utf-8"
            elif full.suffix == ".html":
                ctype = "text/html; charset=utf-8"
            self._send_bytes(HTTPStatus.OK, ctype or "application/octet-stream", full.read_bytes())

        def do_GET(self):
            parsed = urlparse(self.path)
            path = parsed.path
            qs = parse_qs(parsed.query)

            if not path.startswith("/api/"):
                self._serve_static(path)
                return

            if path == "/api/maps":
                self._send_json({
                    "maps": ctx.list_maps(),
                    "maps_root": str(ctx.maps_root),
                    "overrides_root": str(ctx.overrides_root),
                    "hd_terrain_dir": str(ctx.hd_terrain_dir),
                })
                return

            parts = [p for p in path.split("/") if p]
            # /api/map/<name>/<resource>
            if len(parts) == 4 and parts[0] == "api" and parts[1] == "map":
                map_name = parts[2]
                res = parts[3]

                if res == "meta":
                    bundle = ctx.get_bundle(map_name)
                    if bundle is None:
                        self._send_error_json(HTTPStatus.NOT_FOUND, f"Map not found: {map_name}")
                        return
                    hd_found, total_tiles = map_io.count_hd_tiles_for_map(
                        bundle.vanilla_dir, map_name, ctx.hd_terrain_dir
                    )
                    usage = {}
                    for o in bundle.objects:
                        mid = o["model_id"]
                        usage[mid] = usage.get(mid, 0) + 1
                    models_meta = [
                        {
                            "model_id": m["model_id"],
                            "radius": m["radius"],
                            "height": m["height"],
                            "bbox": m["bbox"],
                            "tri_count": m["tri_count"],
                            "byte_size": m["byte_size"],
                            "usage_count": usage.get(m["model_id"], 0),
                        }
                        for m in bundle.models
                    ]
                    self._send_json({
                        "map_name": map_name,
                        "has_overrides": bundle.has_overrides,
                        "override_dir": str(bundle.override_dir),
                        "world_min": map_io.WORLD_MIN,
                        "world_max": map_io.WORLD_MAX,
                        "world_size": map_io.WORLD_SIZE,
                        "tile_world_size": map_io.TILE_WORLD_SIZE,
                        "efa_dim": map_io.EFA_DIM,
                        "height_scale": bundle.height_scale,
                        "hd_tiles": hd_found,
                        "total_tiles": total_tiles,
                        "objects": bundle.objects,
                        "models": models_meta,
                    })
                    return

                if res == "models.json":
                    bundle = ctx.get_bundle(map_name)
                    if bundle is None:
                        self._send_error_json(HTTPStatus.NOT_FOUND, f"Map not found: {map_name}")
                        return
                    self._send_json({"map_name": map_name, "models": bundle.models})
                    return

                if res == "heightmap.bin":
                    bundle = ctx.get_bundle(map_name)
                    if bundle is None or bundle.heightmap is None:
                        self._send_error_json(HTTPStatus.NOT_FOUND, f"Heightmap not found: {map_name}")
                        return
                    step = int(qs.get("step", ["2"])[0])
                    step = max(1, min(4, step))
                    arr = bundle.heightmap.grid_world_f32(downsample=step)
                    self._send_bytes(
                        HTTPStatus.OK,
                        "application/octet-stream",
                        arr.tobytes(),
                        extra_headers={"X-Grid-Dim": str(arr.shape[0])},
                    )
                    return

                if res == "efa.bin":
                    bundle = ctx.get_bundle(map_name)
                    if bundle is None:
                        self._send_error_json(HTTPStatus.NOT_FOUND, f"Map not found: {map_name}")
                        return
                    self._send_bytes(HTTPStatus.OK, "application/octet-stream", map_io.serialize_efa(bundle.efa))
                    return

                if res == "terrain.png":
                    mode = qs.get("mode", ["hd"])[0].lower()
                    size = int(qs.get("size", ["2048"])[0])
                    png = ctx.get_terrain_png(map_name, use_hd=(mode != "vanilla"), size=size)
                    if png is None:
                        self._send_error_json(HTTPStatus.NOT_FOUND, f"Terrain not found: {map_name}")
                        return
                    self._send_bytes(HTTPStatus.OK, "image/png", png)
                    return

                if res == "terrain_atlas.jpg":
                    mode = qs.get("mode", ["hd"])[0].lower()
                    tile_px = int(qs.get("tile_px", ["128"])[0])
                    atlas = ctx.get_terrain_atlas(map_name, use_hd=(mode != "vanilla"), tile_px=tile_px)
                    if atlas is None:
                        self._send_error_json(HTTPStatus.NOT_FOUND, f"Terrain atlas not found: {map_name}")
                        return
                    jpg_bytes, cols, tile_count, actual_tile_px, _ = atlas
                    self._send_bytes(
                        HTTPStatus.OK,
                        "image/jpeg",
                        jpg_bytes,
                        extra_headers={
                            "X-Atlas-Cols": str(cols),
                            "X-Tile-Count": str(tile_count),
                            "X-Tile-Px": str(actual_tile_px),
                        },
                    )
                    return

                if res == "tilemap.bin":
                    atlas = ctx.get_terrain_atlas(map_name, use_hd=True, tile_px=128)
                    if atlas is None:
                        self._send_error_json(HTTPStatus.NOT_FOUND, f"Tilemap not found: {map_name}")
                        return
                    _, cols, tile_count, actual_tile_px, tilemap_bytes = atlas
                    self._send_bytes(
                        HTTPStatus.OK,
                        "application/octet-stream",
                        tilemap_bytes,
                        extra_headers={
                            "X-Atlas-Cols": str(cols),
                            "X-Tile-Count": str(tile_count),
                            "X-Tile-Px": str(actual_tile_px),
                        },
                    )
                    return

            self._send_error_json(HTTPStatus.NOT_FOUND, f"Unknown endpoint: {path}")

        def do_POST(self):
            parsed = urlparse(self.path)
            path = parsed.path
            parts = [p for p in path.split("/") if p]

            if len(parts) == 4 and parts[0] == "api" and parts[1] == "map":
                map_name = parts[2]
                action = parts[3]

                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length) if length > 0 else b"{}"
                try:
                    body = json.loads(raw.decode("utf-8"))
                except Exception as exc:
                    self._send_error_json(HTTPStatus.BAD_REQUEST, f"Invalid JSON: {exc}")
                    return

                if action == "save":
                    objects = body.get("objects", [])
                    efa_b64 = body.get("efa_b64", "")
                    try:
                        efa_bytes = base64.b64decode(efa_b64) if efa_b64 else bytes([255] * map_io.EFA_BYTES)
                        result = ctx.save_map(map_name, objects, efa_bytes)
                        self._send_json(result)
                    except Exception as exc:
                        self._send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, str(exc))
                    return

                if action == "revert":
                    try:
                        result = ctx.revert_map(map_name)
                        self._send_json(result)
                    except Exception as exc:
                        self._send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, str(exc))
                    return

            self._send_error_json(HTTPStatus.NOT_FOUND, f"Unknown POST endpoint: {path}")

    return MapEditorHandler


def run_server(
    maps_root: Path,
    hd_terrain_dir: Path,
    overrides_root: Path,
    host: str = "127.0.0.1",
    port: int = 8765,
) -> ThreadingHTTPServer:
    ctx = EditorContext(maps_root=maps_root, hd_terrain_dir=hd_terrain_dir, overrides_root=overrides_root)
    server = ThreadingHTTPServer((host, port), make_handler(ctx))
    return server
