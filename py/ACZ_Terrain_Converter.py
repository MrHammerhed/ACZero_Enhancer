#!/usr/bin/env python3
"""
Ace Combat Zero: The Belkan War — Terrain Texture Exporter & Slicer (v1.0)
==========================================================================
Converts PS2/PC terrain containers (mXX.mtd + mXX.mtc + mXX.mti + mXX.mta)
into seamless context-padded 512x512 (or 256x256) sheets sized for AI upscalers
with a 2048x2048 export limit, and slices upscaled sheets back into individual
HD tiles named by their 64-bit FNV-1a pixel hash for acz_hdtex.asi.

Usage:
  1. Export seamless 512x512 sheets (6x6 core + 1-tile seamless border = 8x8 -> 2048x2048 at 4x):
       python py/ACZ_Terrain_Converter.py export --map-dir "extracted/a/data/map/m02a" --out "work/m02a_sheets"
     Or export ALL maps in extracted/a/data/map:
       python py/ACZ_Terrain_Converter.py export --all-maps "extracted/a/data/map" --out "work/all_sheets"

  2. Upscale all PNGs in work/m02a_sheets/ (e.g. 4x -> 2048x2048) into work/m02a_upscaled/

  3. Slice upscaled sheets back into mods/textures/terrain/<HASH>.png:
       python py/ACZ_Terrain_Converter.py slice --manifest "work/m02a_sheets/manifest.json" --upscaled "work/m02a_upscaled" --out "mods/textures/terrain"

  4. Optional: export raw unique 64x64 tiles directly (for quick testing):
       python py/ACZ_Terrain_Converter.py export-tiles --map-dir "extracted/a/data/map/m02a" --out "work/m02a_tiles"
"""

import argparse
import json
import math
import os
import struct
import sys
from collections import deque
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    print("ERROR: Pillow (PIL) is required. Install it with: pip install Pillow", file=sys.stderr)
    sys.exit(1)

try:
    import numpy as np
except ImportError:
    np = None

FNV1A_64_OFFSET = 0xCBF29CE484222325
FNV1A_64_PRIME  = 0x00000100000001B3
FNV1A_64_MASK   = 0xFFFFFFFFFFFFFFFF

TILE_SIZE = 64
TILE_BYTES = TILE_SIZE * TILE_SIZE  # 4096 bytes (8-bit indexed PSMT8)


def fnv1a_64(data: bytes) -> int:
    h = FNV1A_64_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV1A_64_PRIME) & FNV1A_64_MASK
    return h


def unswizzle_csm1(clut_bytes: bytes) -> list:
    """Unswizzle PS2 GS CSM1 256-color RGBA32 CLUT and force opaque alpha (255)."""
    if len(clut_bytes) < 1024:
        raise ValueError(f"MTC palette too small: {len(clut_bytes)} bytes (expected 1024)")
    palette = [None] * 256
    for i in range(256):
        block = i // 32
        entry = i % 32
        if 8 <= entry < 16:
            orig_i = block * 32 + entry + 8
        elif 16 <= entry < 24:
            orig_i = block * 32 + entry - 8
        else:
            orig_i = i
        r = clut_bytes[orig_i * 4 + 0]
        g = clut_bytes[orig_i * 4 + 1]
        b = clut_bytes[orig_i * 4 + 2]
        # PS2 terrain palette alpha is 0x80 (= 100% opaque on PS2 GS)
        palette[i] = (r, g, b, 255)
    return palette


def load_map_terrain(map_dir: Path):
    """Load mXX.mtd, mXX.mtc, mXX.mti, and optional mXX.mta from map_dir."""
    map_name = map_dir.name
    mtd_path = map_dir / f"{map_name}.mtd"
    mtc_path = map_dir / f"{map_name}.mtc"
    mti_path = map_dir / f"{map_name}.mti"
    mta_path = map_dir / f"{map_name}.mta"

    if not (mtd_path.is_file() and mtc_path.is_file() and mti_path.is_file()):
        return None

    mtd_bytes = mtd_path.read_bytes()
    mtc_bytes = mtc_path.read_bytes()
    mti_bytes = mti_path.read_bytes()
    mta_bytes = mta_path.read_bytes() if mta_path.is_file() else None

    tile_count, data_offset, data_size = struct.unpack_from("<III", mtd_bytes, 0)
    palette = unswizzle_csm1(mtc_bytes)

    tiles_raw = []
    tiles_hash = []
    for tid in range(tile_count):
        off = data_offset + tid * TILE_BYTES
        raw = mtd_bytes[off : off + TILE_BYTES]
        tiles_raw.append(raw)
        tiles_hash.append(f"{fnv1a_64(raw):016X}")

    # Decode MTI entries: uint16_t where bits 0..9 (& 0x03FF) = tile_id
    mti_count = len(mti_bytes) // 2
    mti_u16 = struct.unpack(f"<{mti_count}H", mti_bytes)

    return {
        "map_name": map_name,
        "tile_count": tile_count,
        "palette": palette,
        "tiles_raw": tiles_raw,
        "tiles_hash": tiles_hash,
        "mti_u16": mti_u16,
        "mta_bytes": mta_bytes,
    }


def decode_tile_image(raw_tile: bytes, palette: list) -> Image.Image:
    """Convert 4096-byte 8-bit indexed tile into a 64x64 RGBA Pillow Image."""
    rgba = bytearray(TILE_BYTES * 4)
    for idx, b in enumerate(raw_tile):
        r, g, b_ch, a = palette[b]
        o = idx * 4
        rgba[o + 0] = r
        rgba[o + 1] = g
        rgba[o + 2] = b_ch
        rgba[o + 3] = a
    return Image.frombytes("RGBA", (TILE_SIZE, TILE_SIZE), bytes(rgba))


def build_world_tile_grid(map_data: dict):
    """
    Build the full 256x256 world sub-tile grid using mXX.mta (16x16 coarse indices)
    and mXX.mti (each coarse sector = 16x16 sub-tiles, tile_id = entry & 0x03FF).
    Returns a 256x256 list of lists of tile_ids (or None if out of bounds).
    """
    mti_u16 = map_data["mti_u16"]
    mta_bytes = map_data["mta_bytes"]
    tile_count = map_data["tile_count"]
    num_coarse = len(mti_u16) // 256

    grid = [[None] * 256 for _ in range(256)]
    if mta_bytes and len(mta_bytes) == 256:
        for cy in range(16):
            for cx in range(16):
                coarse_idx = mta_bytes[cy * 16 + cx]
                if coarse_idx >= num_coarse:
                    continue
                base_cell = coarse_idx * 256
                for sy in range(16):
                    wy = cy * 16 + sy
                    row_off = base_cell + sy * 16
                    for sx in range(16):
                        wx = cx * 16 + sx
                        tid = mti_u16[row_off + sx] & 0x03FF
                        if tid < tile_count:
                            grid[wy][wx] = tid
    else:
        # Fallback: lay out coarse sectors sequentially if .mta is absent
        for coarse_idx in range(min(num_coarse, 256)):
            cy = coarse_idx // 16
            cx = coarse_idx % 16
            base_cell = coarse_idx * 256
            for sy in range(16):
                wy = cy * 16 + sy
                row_off = base_cell + sy * 16
                for sx in range(16):
                    wx = cx * 16 + sx
                    tid = mti_u16[row_off + sx] & 0x03FF
                    if tid < tile_count:
                        grid[wy][wx] = tid
    return grid


def gaussian_blur_f32(arr: "np.ndarray", sigma: float) -> "np.ndarray":
    """Separable 1D Gaussian blur on float32 HxWx3 array using reflect padding."""
    if sigma <= 0.0:
        return arr
    radius = max(1, int(math.ceil(3.0 * sigma)))
    x = np.arange(-radius, radius + 1, dtype=np.float32)
    kernel = np.exp(-0.5 * (x / sigma) ** 2)
    kernel /= kernel.sum()

    pad_x = np.pad(arr, ((0, 0), (radius, radius), (0, 0)), mode="reflect")
    tmp = np.zeros_like(arr, dtype=np.float32)
    for i, w in enumerate(kernel):
        tmp += w * pad_x[:, i : i + arr.shape[1], :]

    pad_y = np.pad(tmp, ((radius, radius), (0, 0), (0, 0)), mode="reflect")
    out = np.zeros_like(arr, dtype=np.float32)
    for i, w in enumerate(kernel):
        out += w * pad_y[i : i + arr.shape[0], :, :]
    return out


def downsample_box_f32(arr: "np.ndarray", target_w: int, target_h: int) -> "np.ndarray":
    """Box-downsample float32 HxWx3 array to (target_h, target_w, 3)."""
    h, w = arr.shape[:2]
    if w == target_w and h == target_h:
        return arr
    if w % target_w == 0 and h % target_h == 0:
        fx = w // target_w
        fy = h // target_h
        return arr.reshape(target_h, fy, target_w, fx, 3).mean(axis=(1, 3))
    chans = [
        np.asarray(
            Image.fromarray(arr[:, :, c], mode="F").resize(
                (target_w, target_h), Image.Resampling.BOX
            ),
            dtype=np.float32,
        )
        for c in range(3)
    ]
    return np.stack(chans, axis=2)


def color_match_region_f32(
    u_reg: "np.ndarray",
    o_reg: "np.ndarray",
    border_o: int,
    tile_px_o: int,
    mode: str,
    sigma_o: float,
):
    """
    Lock upscaled region RGB palette & low-frequency tone to original region.
    Returns (corrected_float32, raw_delta_rgb, post_mae_rgb).
    """
    ho, wo = o_reg.shape[:2]
    hu, wu = u_reg.shape[:2]
    u_down = downsample_box_f32(u_reg, wo, ho)

    bx = border_o if wo > 2 * border_o else 0
    by = border_o if ho > 2 * border_o else 0
    oc = o_reg[by : ho - by, bx : wo - bx]
    uc = u_down[by : ho - by, bx : wo - bx]

    o_m = oc.mean(axis=(0, 1))
    o_s = oc.std(axis=(0, 1))
    u_m = uc.mean(axis=(0, 1))
    u_s = uc.std(axis=(0, 1))
    raw_delta_rgb = u_m - o_m

    gain = np.clip(o_s / np.maximum(u_s, 1e-4), 0.85, 1.20)
    base = (u_reg - u_m) * gain + o_m

    if mode == "hybrid":
        base_down = downsample_box_f32(base, wo, ho)
        o_blur = gaussian_blur_f32(o_reg, sigma_o)
        u_blur = gaussian_blur_f32(base_down, sigma_o)
        d_o = np.clip(o_blur - u_blur, -18.0, 18.0)
        chans = [
            np.asarray(
                Image.fromarray(d_o[:, :, c], mode="F").resize(
                    (wu, hu), Image.Resampling.BILINEAR
                ),
                dtype=np.float32,
            )
            for c in range(3)
        ]
        out = np.clip(base + np.stack(chans, axis=2), 0.0, 255.0)
    else:
        out = np.clip(base, 0.0, 255.0)

    out_down = downsample_box_f32(out, wo, ho)
    out_c = out_down[by : ho - by, bx : wo - bx]
    ih, iw = oc.shape[:2]
    if tile_px_o > 0 and ih % tile_px_o == 0 and iw % tile_px_o == 0:
        ny = ih // tile_px_o
        nx = iw // tile_px_o
        om_tiles = oc.reshape(ny, tile_px_o, nx, tile_px_o, 3).mean(axis=(1, 3))
        um_tiles = out_c.reshape(ny, tile_px_o, nx, tile_px_o, 3).mean(axis=(1, 3))
        post_mae_rgb = float(np.abs(um_tiles - om_tiles).mean())
    else:
        post_mae_rgb = float(np.abs(out_c.mean(axis=(0, 1)) - o_m).mean())
    return out, raw_delta_rgb, post_mae_rgb


def apply_sheet_color_match(
    up_img: Image.Image,
    orig_path: Path,
    entry: dict,
    mode: str,
    sigma_o: float,
):
    """Apply region-aware color-lock to an upscaled sheet against original 512x512 sheet."""
    if mode == "none":
        return up_img, None
    if np is None:
        raise RuntimeError(
            "NumPy is required for --color-match hybrid/affine (install numpy or pass --color-match none)"
        )
    if not orig_path.is_file():
        raise FileNotFoundError(f"Original sheet not found for color matching: {orig_path}")

    with Image.open(orig_path) as oim:
        o_rgb = np.asarray(oim.convert("RGB"), dtype=np.float32)

    rgba = np.asarray(up_img.convert("RGBA"), dtype=np.uint8).copy()
    u_rgb = rgba[:, :, :3].astype(np.float32)

    grid_w = entry["grid_w"]
    grid_h = entry["grid_h"]
    oh, ow = o_rgb.shape[:2]
    uh, uw = u_rgb.shape[:2]
    orig_tw, orig_th = ow // grid_w, oh // grid_h
    up_tw, up_th = uw // grid_w, uh // grid_h
    tile_px_o = min(orig_tw, orig_th)

    regions = entry.get("regions")
    if not regions:
        regions = [{
            "x": 0,
            "y": 0,
            "w": grid_w,
            "h": grid_h,
            "border": entry.get("border", 0),
        }]

    raw_deltas = []
    post_maes = []
    for reg in regions:
        if reg.get("filler"):
            continue
        rx, ry, rw, rh = reg["x"], reg["y"], reg["w"], reg["h"]
        border_tiles = reg.get("border", 0)
        ox0, oy0, ox1, oy1 = rx * orig_tw, ry * orig_th, (rx + rw) * orig_tw, (ry + rh) * orig_th
        ux0, uy0, ux1, uy1 = rx * up_tw, ry * up_th, (rx + rw) * up_tw, (ry + rh) * up_th
        border_o = int(round(border_tiles * tile_px_o))

        out_reg, raw_d, post_m = color_match_region_f32(
            u_rgb[uy0:uy1, ux0:ux1],
            o_rgb[oy0:oy1, ox0:ox1],
            border_o,
            tile_px_o,
            mode,
            sigma_o,
        )
        rgba[uy0:uy1, ux0:ux1, :3] = np.rint(out_reg).astype(np.uint8)
        raw_deltas.append(raw_d)
        post_maes.append(post_m)

    if not raw_deltas:
        return Image.fromarray(rgba, mode="RGBA"), None

    avg_d = np.mean(raw_deltas, axis=0)
    avg_post = float(np.mean(post_maes))
    stats = (
        f"raw_shift=[{avg_d[0]:+.1f},{avg_d[1]:+.1f},{avg_d[2]:+.1f}], "
        f"post_tile_mae={avg_post:.2f}"
    )
    return Image.fromarray(rgba, mode="RGBA"), stats


def resolve_upscaled_sheets(
    sheets: list,
    orig_dir: Path,
    upscaled_dir: Path,
    auto_match: bool,
    max_mae: float,
    verbose: bool = True,
):
    """Resolve manifest entries to existing upscaled files (exact name, ext fallback, or --auto-match)."""
    auto_matched = {}
    if auto_match and upscaled_dir.is_dir():
        valid_exts = {".png", ".jpg", ".jpeg", ".webp"}
        sheet_stems = {Path(e["file"]).stem.lower() for e in sheets}
        candidates = sorted(
            p for p in upscaled_dir.iterdir()
            if p.is_file() and p.suffix.lower() in valid_exts and p.stem.lower() not in sheet_stems
        )
        if candidates:
            orig_thumbs = []
            for e in sheets:
                orig_p = orig_dir / e["file"]
                if orig_p.is_file():
                    with Image.open(orig_p) as oimg:
                        orig_thumbs.append((
                            e["file"],
                            oimg.convert("RGB").resize((64, 64), Image.Resampling.BILINEAR),
                        ))
            if orig_thumbs:
                auto_matched_mae = {}
                for cand_path in candidates:
                    with Image.open(cand_path) as cimg:
                        cand_thumb = cimg.convert("RGB").resize((64, 64), Image.Resampling.BILINEAR)
                    best_fname = None
                    best_mae = float("inf")
                    for fname, orig_thumb in orig_thumbs:
                        diff = ImageChops.difference(cand_thumb, orig_thumb)
                        mae = sum(ImageStat.Stat(diff).mean) / 3.0
                        if mae < best_mae:
                            best_mae = mae
                            best_fname = fname
                    if best_fname is not None and best_mae <= max_mae:
                        prev_mae = auto_matched_mae.get(best_fname)
                        if prev_mae is None or best_mae < prev_mae:
                            auto_matched[best_fname] = cand_path
                            auto_matched_mae[best_fname] = best_mae
                            if verbose:
                                print(f"  [auto-match] {cand_path.name} -> {best_fname} (MAE={best_mae:.2f})")
                    else:
                        print(
                            f"WARNING: Skipped {cand_path.name} — best match {best_fname} "
                            f"exceeded max MAE ({best_mae:.2f} > {max_mae:.2f})",
                            file=sys.stderr,
                        )

    matched = []
    missing = 0
    for entry in sheets:
        fname = entry["file"]
        src_path = upscaled_dir / fname
        if not src_path.is_file():
            stem = Path(fname).stem
            alt = None
            for ext in (".png", ".PNG", ".webp", ".jpg", ".jpeg"):
                cand = upscaled_dir / f"{stem}{ext}"
                if cand.is_file():
                    alt = cand
                    break
            if alt is None:
                alt = auto_matched.get(fname)
            if alt is None:
                missing += 1
                continue
            src_path = alt
        matched.append((entry, src_path))
    return matched, missing


def norm_coord(v: float):
    """Return exact int when v represents a whole tile coordinate, else float."""
    r = round(float(v))
    return int(r) if abs(v - r) < 1e-6 else round(float(v), 4)


def render_world_patch(
    world: list,
    get_tile_img,
    wx0: int,
    wy0: int,
    patch_tiles: int,
    border: float,
    fallback_tid: int,
) -> Image.Image:
    """Render a patch_tiles x patch_tiles RGBA patch with mirror-reflected world edges."""
    patch_px = patch_tiles * TILE_SIZE
    patch_img = Image.new("RGBA", (patch_px, patch_px), (0, 0, 0, 255))
    border_px = int(round(border * TILE_SIZE))
    pad_tiles = int(math.ceil(border))
    core_tiles = int(round(patch_tiles - 2.0 * border))
    for ty in range(-pad_tiles, core_tiles + pad_tiles):
        raw_wy = wy0 + ty
        flip_y = False
        if raw_wy < 0:
            wy = min(255, -raw_wy - 1)
            flip_y = True
        elif raw_wy >= 256:
            wy = max(0, 511 - raw_wy)
            flip_y = True
        else:
            wy = raw_wy
        dst_y = border_px + ty * TILE_SIZE
        y0 = max(0, dst_y)
        y1 = min(patch_px, dst_y + TILE_SIZE)
        if y0 >= y1:
            continue
        src_y0 = y0 - dst_y
        src_h = y1 - y0

        for tx in range(-pad_tiles, core_tiles + pad_tiles):
            raw_wx = wx0 + tx
            flip_x = False
            if raw_wx < 0:
                wx = min(255, -raw_wx - 1)
                flip_x = True
            elif raw_wx >= 256:
                wx = max(0, 511 - raw_wx)
                flip_x = True
            else:
                wx = raw_wx

            dst_x = border_px + tx * TILE_SIZE
            x0 = max(0, dst_x)
            x1 = min(patch_px, dst_x + TILE_SIZE)
            if x0 >= x1:
                continue
            src_x0 = x0 - dst_x

            tid = world[wy][wx]
            if tid is None:
                tid = fallback_tid
            t_img = get_tile_img(tid)
            if flip_x:
                t_img = t_img.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if flip_y:
                t_img = t_img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            tile_patch = t_img.crop((src_x0, src_y0, src_x0 + (x1 - x0), src_y0 + src_h))
            patch_img.paste(tile_patch, (x0, y0))
    return patch_img


def export_map_sheets(
    map_data: dict,
    out_dir: Path,
    grid_size: int,
    border: float,
    global_seen_hashes: set,
    manifest_sheets: list,
    pack_mode: str = "quad",
    min_full_tiles: int = 12,
    seed_entries: list = None,
):
    """
    Export seamless context-padded sheets for a single map.
    Default: grid_size=8, border=0.5 -> 7x7 core tiles + 32px seamless border on all 4 sides
    = 512x512 px -> 2048x2048 px at 4x upscale!
    """
    map_name = map_data["map_name"]
    palette = map_data["palette"]
    tiles_raw = map_data["tiles_raw"]
    tiles_hash = map_data["tiles_hash"]
    tile_count = map_data["tile_count"]

    if border < 0.0:
        raise ValueError(f"border ({border}) must be >= 0")
    border_px = int(round(border * TILE_SIZE))
    if abs(border * TILE_SIZE - border_px) > 1e-6:
        raise ValueError(f"border ({border}) * TILE_SIZE ({TILE_SIZE}) must be an integer pixel count")
    core_f = grid_size - 2.0 * border
    core = int(round(core_f))
    if abs(core_f - core) > 1e-6 or core <= 0:
        raise ValueError(
            f"grid_size ({grid_size}) - 2 * border ({border}) must be a positive integer core tile count"
        )
    sheet_border_val = norm_coord(border)

    # Lazy tile image cache per map
    tile_img_cache = {}

    def get_tile_img(tid: int) -> Image.Image:
        img = tile_img_cache.get(tid)
        if img is None:
            img = decode_tile_image(tiles_raw[tid], palette)
            tile_img_cache[tid] = img
        return img

    world = build_world_tile_grid(map_data)
    sheet_idx = 0
    exported_hashes_this_map = set()

    def claim_window_tiles(
        wx0: int,
        wy0: int,
        w_core: int,
        h_core: int,
        col_off: float,
        row_off: float,
        claimed_in_sheet: list,
        core_in_sheet: list,
        seen_in_sheet_core: set,
    ) -> None:
        for dy in range(h_core):
            wy = wy0 + dy
            if wy < 0 or wy >= 256:
                continue
            for dx in range(w_core):
                wx = wx0 + dx
                if wx < 0 or wx >= 256:
                    continue
                tid = world[wy][wx]
                if tid is None:
                    continue
                th = tiles_hash[tid]
                rec = {
                    "col": norm_coord(col_off + dx),
                    "row": norm_coord(row_off + dy),
                    "tile_id": tid,
                    "hash": th,
                }
                if th not in global_seen_hashes:
                    global_seen_hashes.add(th)
                    exported_hashes_this_map.add(th)
                    claimed_in_sheet.append(rec)
                if th not in seen_in_sheet_core:
                    seen_in_sheet_core.add(th)
                    core_in_sheet.append(rec)

    def emit_sheet(
        sheet_img: Image.Image,
        layout: str,
        sheet_border,
        world_origin,
        regions: list,
        claimed_in_sheet: list,
        core_in_sheet: list,
    ) -> None:
        nonlocal sheet_idx
        sheet_filename = f"{map_name}_sheet_{sheet_idx:03d}.png"
        sheet_img.save(out_dir / sheet_filename)
        manifest_sheets.append({
            "file": sheet_filename,
            "map": map_name,
            "layout": layout,
            "grid_w": grid_size,
            "grid_h": grid_size,
            "border": sheet_border,
            "tile_orig_size": TILE_SIZE,
            "world_origin": world_origin,
            "regions": regions,
            "tiles": claimed_in_sheet,
            "core_tiles": core_in_sheet,
        })
        sheet_idx += 1

    def build_candidate_windows(w_core: int, h_core: int, b: float, unc_set: set):
        pad = int(math.ceil(b))
        windows = {}
        for wy0 in range(0, 256 - h_core + 1):
            for wx0 in range(0, 256 - w_core + 1):
                hset = set()
                for dy in range(h_core):
                    row_w = world[wy0 + dy]
                    for dx in range(w_core):
                        tid = row_w[wx0 + dx]
                        if tid is not None:
                            th = tiles_hash[tid]
                            if th in unc_set:
                                hset.add(th)
                if not hset:
                    continue
                fs = frozenset(hset)
                # Prefer interior windows over mirrored map borders when coverage ties
                pen = (
                    (1 if wx0 < pad else 0)
                    + (1 if wx0 + w_core + pad > 256 else 0)
                    + (1 if wy0 < pad else 0)
                    + (1 if wy0 + h_core + pad > 256 else 0)
                )
                cand = (pen, wy0, wx0)
                prev = windows.get(fs)
                if prev is None or cand < prev:
                    windows[fs] = cand
        return [(fs, pen, wx0, wy0) for fs, (pen, wy0, wx0) in windows.items()]

    def pick_best_window(candidates: list, unc_set: set):
        best_key = None
        best = None
        for fs, pen, wx0, wy0 in candidates:
            cov = len(fs & unc_set)
            if cov == 0:
                continue
            key = (cov, -pen, -wy0, -wx0)
            if best_key is None or key > best_key:
                best_key = key
                best = (cov, fs, pen, wx0, wy0)
        return best

    if pack_mode == "grid":
        # Legacy fixed-stride scan across the 256x256 world grid
        for wy0 in range(0, 256, core):
            for wx0 in range(0, 256, core):
                claimed_in_sheet = []
                core_in_sheet = []
                seen_in_sheet_core = set()
                # Check first without mutating global_seen_hashes if empty
                has_new = False
                for dy in range(core):
                    wy = wy0 + dy
                    if wy >= 256:
                        continue
                    for dx in range(core):
                        wx = wx0 + dx
                        if wx >= 256:
                            continue
                        tid = world[wy][wx]
                        if tid is not None and tiles_hash[tid] not in global_seen_hashes:
                            has_new = True
                            break
                    if has_new:
                        break
                if not has_new:
                    continue
                claim_window_tiles(
                    wx0, wy0, core, core, border, border,
                    claimed_in_sheet, core_in_sheet, seen_in_sheet_core,
                )
                sheet_img = render_world_patch(
                    world, get_tile_img, wx0, wy0, grid_size, border,
                    claimed_in_sheet[0]["tile_id"],
                )
                emit_sheet(
                    sheet_img,
                    "full",
                    sheet_border_val,
                    [wx0, wy0],
                    [{"x": 0, "y": 0, "w": grid_size, "h": grid_size, "border": sheet_border_val, "world_origin": [wx0, wy0]}],
                    claimed_in_sheet,
                    core_in_sheet,
                )
    else:
        # Step A: Seed already-upscaled sheets first so completed work is preserved
        for se in seed_entries or []:
            if se.get("grid_w", grid_size) != grid_size or se.get("grid_h", grid_size) != grid_size:
                continue
            se_layout = se.get("layout", "full")
            if se_layout == "quad" and se.get("regions"):
                sheet_px = grid_size * TILE_SIZE
                sheet_img = Image.new("RGBA", (sheet_px, sheet_px), (0, 0, 0, 255))
                claimed_in_sheet, core_in_sheet, seen_core = [], [], set()
                regions = []
                last_patch = None
                for reg in se["regions"]:
                    qx, qy, rw, rh = reg["x"], reg["y"], reg["w"], reg["h"]
                    rb = float(reg.get("border", border))
                    rb_val = norm_coord(rb)
                    if reg.get("filler") or reg.get("world_origin") is None:
                        if last_patch is not None:
                            sheet_img.paste(last_patch, (qx * TILE_SIZE, qy * TILE_SIZE))
                        regions.append({
                            "x": qx, "y": qy, "w": rw, "h": rh,
                            "border": rb_val, "world_origin": None, "filler": True,
                        })
                    else:
                        wx0, wy0 = reg["world_origin"]
                        rc_w = int(round(rw - 2.0 * rb))
                        rc_h = int(round(rh - 2.0 * rb))
                        claim_window_tiles(
                            wx0, wy0, rc_w, rc_h, qx + rb, qy + rb,
                            claimed_in_sheet, core_in_sheet, seen_core,
                        )
                        fb_tid = core_in_sheet[-1]["tile_id"] if core_in_sheet else 0
                        patch_img = render_world_patch(
                            world, get_tile_img, wx0, wy0, rw, rb, fb_tid
                        )
                        last_patch = patch_img
                        sheet_img.paste(patch_img, (qx * TILE_SIZE, qy * TILE_SIZE))
                        regions.append({
                            "x": qx, "y": qy, "w": rw, "h": rh,
                            "border": rb_val, "world_origin": [wx0, wy0],
                        })
                if claimed_in_sheet:
                    sb_val = norm_coord(float(se.get("border", border)))
                    emit_sheet(
                        sheet_img, "quad", sb_val, None, regions, claimed_in_sheet, core_in_sheet
                    )
            elif se.get("world_origin") is not None:
                wx0, wy0 = se["world_origin"]
                sb = float(se.get("border", border))
                sb_val = norm_coord(sb)
                sc = int(round(grid_size - 2.0 * sb))
                claimed_in_sheet, core_in_sheet, seen_core = [], [], set()
                claim_window_tiles(
                    wx0, wy0, sc, sc, sb, sb,
                    claimed_in_sheet, core_in_sheet, seen_core,
                )
                if claimed_in_sheet:
                    sheet_img = render_world_patch(
                        world, get_tile_img, wx0, wy0, grid_size, sb,
                        claimed_in_sheet[0]["tile_id"],
                    )
                    emit_sheet(
                        sheet_img,
                        "full",
                        sb_val,
                        [wx0, wy0],
                        [{"x": 0, "y": 0, "w": grid_size, "h": grid_size, "border": sb_val, "world_origin": [wx0, wy0]}],
                        claimed_in_sheet,
                        core_in_sheet,
                    )

        # Remaining uncovered hashes present in the world grid for this map
        uncovered = {
            tiles_hash[world[wy][wx]]
            for wy in range(256)
            for wx in range(256)
            if world[wy][wx] is not None and tiles_hash[world[wy][wx]] not in global_seen_hashes
        }

        quad_core_f = 4.0 - 2.0 * border
        quad_core = int(round(quad_core_f))
        use_quad = (
            pack_mode == "quad"
            and grid_size == 8
            and abs(quad_core_f - quad_core) < 1e-6
            and quad_core > 0
        )

        # Step B/C: Greedy full core x core sheets while new-tile yield >= threshold
        if uncovered:
            full_cands = build_candidate_windows(core, core, border, uncovered)
            while uncovered:
                picked = pick_best_window(full_cands, uncovered)
                if picked is None:
                    break
                cov, fs, _, wx0, wy0 = picked
                if use_quad and cov < min_full_tiles:
                    break
                uncovered -= fs
                claimed_in_sheet, core_in_sheet, seen_core = [], [], set()
                claim_window_tiles(
                    wx0, wy0, core, core, border, border,
                    claimed_in_sheet, core_in_sheet, seen_core,
                )
                sheet_img = render_world_patch(
                    world, get_tile_img, wx0, wy0, grid_size, border,
                    claimed_in_sheet[0]["tile_id"],
                )
                emit_sheet(
                    sheet_img,
                    "full",
                    sheet_border_val,
                    [wx0, wy0],
                    [{"x": 0, "y": 0, "w": grid_size, "h": grid_size, "border": sheet_border_val, "world_origin": [wx0, wy0]}],
                    claimed_in_sheet,
                    core_in_sheet,
                )

        # Step D: Pack remaining sparse tiles as 4 quadrant blocks (e.g. 3x3 w/ 0.5-tile border), 4 per sheet
        if use_quad and uncovered:
            quad_cands = build_candidate_windows(quad_core, quad_core, border, uncovered)
            quad_blocks = []
            while uncovered:
                picked = pick_best_window(quad_cands, uncovered)
                if picked is None:
                    break
                _, fs, _, wx0, wy0 = picked
                uncovered -= fs
                quad_blocks.append((wx0, wy0))

            quad_offsets = [(0, 0), (4, 0), (0, 4), (4, 4)]
            sheet_px = grid_size * TILE_SIZE
            for start in range(0, len(quad_blocks), 4):
                chunk = quad_blocks[start : start + 4]
                sheet_img = Image.new("RGBA", (sheet_px, sheet_px), (0, 0, 0, 255))
                claimed_in_sheet, core_in_sheet, seen_core = [], [], set()
                regions = []
                last_patch = None
                for qi in range(4):
                    qx, qy = quad_offsets[qi]
                    if qi < len(chunk):
                        wx0, wy0 = chunk[qi]
                        claim_window_tiles(
                            wx0, wy0, quad_core, quad_core, qx + border, qy + border,
                            claimed_in_sheet, core_in_sheet, seen_core,
                        )
                        fb_tid = (
                            claimed_in_sheet[-1]["tile_id"]
                            if claimed_in_sheet
                            else (core_in_sheet[-1]["tile_id"] if core_in_sheet else 0)
                        )
                        patch_img = render_world_patch(
                            world, get_tile_img, wx0, wy0, 4, border, fb_tid
                        )
                        last_patch = patch_img
                        sheet_img.paste(patch_img, (qx * TILE_SIZE, qy * TILE_SIZE))
                        regions.append({
                            "x": qx,
                            "y": qy,
                            "w": 4,
                            "h": 4,
                            "border": sheet_border_val,
                            "world_origin": [wx0, wy0],
                        })
                    else:
                        sheet_img.paste(last_patch, (qx * TILE_SIZE, qy * TILE_SIZE))
                        regions.append({
                            "x": qx,
                            "y": qy,
                            "w": 4,
                            "h": 4,
                            "border": sheet_border_val,
                            "world_origin": None,
                            "filler": True,
                        })
                emit_sheet(
                    sheet_img, "quad", sheet_border_val, None, regions, claimed_in_sheet, core_in_sheet
                )

    # Handle any unreferenced tiles in mXX.mtd that never appeared in mXX.mta/mXX.mti
    unreferenced = []
    for tid in range(tile_count):
        th = tiles_hash[tid]
        if th not in global_seen_hashes:
            global_seen_hashes.add(th)
            exported_hashes_this_map.add(th)
            unreferenced.append((tid, th))

    if unreferenced:
        per_sheet = grid_size * grid_size
        for start in range(0, len(unreferenced), per_sheet):
            batch = unreferenced[start : start + per_sheet]
            sheet_px = grid_size * TILE_SIZE
            sheet_img = Image.new("RGBA", (sheet_px, sheet_px), (0, 0, 0, 255))
            claimed = []
            for idx, (tid, th) in enumerate(batch):
                col = idx % grid_size
                row = idx // grid_size
                sheet_img.paste(get_tile_img(tid), (col * TILE_SIZE, row * TILE_SIZE))
                claimed.append({
                    "col": col,
                    "row": row,
                    "tile_id": tid,
                    "hash": th,
                })
            sheet_filename = f"{map_name}_extra_{sheet_idx:03d}.png"
            sheet_img.save(out_dir / sheet_filename)
            manifest_sheets.append({
                "file": sheet_filename,
                "map": map_name,
                "layout": "extra",
                "grid_w": grid_size,
                "grid_h": grid_size,
                "border": 0,
                "tile_orig_size": TILE_SIZE,
                "world_origin": None,
                "regions": [{
                    "x": 0,
                    "y": 0,
                    "w": grid_size,
                    "h": grid_size,
                    "border": 0,
                    "world_origin": None,
                }],
                "tiles": claimed,
                "core_tiles": claimed,
            })
            sheet_idx += 1

    return sheet_idx, len(exported_hashes_this_map)


def deduplicate_map_tiles(
    map_data: dict,
    dedup_mae: float,
    dedup_max_diff: float,
    global_seen_hashes: set,
    dedup_aliases: list,
) -> int:
    """Detect visually near-identical 64x64 tiles within a map (e.g. same base
    template with minor baked lighting/quantization differences). Marks aliases
    in global_seen_hashes so export skips them, and records alias mappings for
    slice-time expansion with per-channel color delta."""
    if dedup_mae <= 0.0:
        return 0

    map_name = map_data["map_name"]
    tiles_raw = map_data["tiles_raw"]
    palette = map_data["palette"]
    tiles_hash = map_data["tiles_hash"]
    world = build_world_tile_grid(map_data)

    # Count world frequency so heavily reused background tiles become canonical
    freq = {}
    for wy in range(256):
        row = world[wy]
        for wx in range(256):
            tid = row[wx]
            if tid is not None:
                freq[tid] = freq.get(tid, 0) + 1

    # One representative tid per unique hash not already seen
    hash_to_tid = {}
    for tid, cnt in sorted(freq.items(), key=lambda kv: (-kv[1], kv[0])):
        th = tiles_hash[tid]
        if th in global_seen_hashes or th in hash_to_tid:
            continue
        hash_to_tid[th] = tid

    canon_hashes = []
    canon_blocks = []  # list of (16*16*3,) float32
    canon_means = []   # list of (3,) float32
    n_aliased = 0

    for th, tid in hash_to_tid.items():
        img = decode_tile_image(tiles_raw[tid], palette).convert("RGB")
        arr = np.asarray(img, dtype=np.float32)  # (64, 64, 3)
        blk = arr.reshape(16, 4, 16, 4, 3).mean(axis=(1, 3)).reshape(-1)
        mean_rgb = arr.mean(axis=(0, 1))

        matched_idx = -1
        matched_mae = 0.0
        if canon_blocks:
            cmat = np.stack(canon_blocks, axis=0)
            diffs = np.abs(cmat - blk)
            maes = diffs.mean(axis=1)
            best_i = int(np.argmin(maes))
            if maes[best_i] <= dedup_mae and float(diffs[best_i].max()) <= dedup_max_diff:
                matched_idx = best_i
                matched_mae = float(maes[best_i])

        if matched_idx >= 0:
            delta = mean_rgb - canon_means[matched_idx]
            global_seen_hashes.add(th)
            dedup_aliases.append({
                "map": map_name,
                "alias_hash": th,
                "canonical_hash": canon_hashes[matched_idx],
                "mae": round(matched_mae, 2),
                "mean_delta": [round(float(delta[0]), 2), round(float(delta[1]), 2), round(float(delta[2]), 2)],
            })
            n_aliased += 1
        else:
            canon_hashes.append(th)
            canon_blocks.append(blk)
            canon_means.append(mean_rgb)

    return n_aliased


def cmd_export(args):
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    map_dirs = []
    if args.map_dir:
        map_dirs.append(Path(args.map_dir))
    elif args.all_maps:
        root = Path(args.all_maps)
        for p in sorted(root.iterdir()):
            if p.is_dir():
                map_dirs.append(p)
    else:
        print("ERROR: Specify either --map-dir or --all-maps", file=sys.stderr)
        sys.exit(1)

    seed_by_map = {}
    if getattr(args, "reuse_manifest", None):
        reuse_manifest_path = Path(args.reuse_manifest)
        if not getattr(args, "reuse_upscaled", None):
            print("ERROR: --reuse-upscaled is required when using --reuse-manifest", file=sys.stderr)
            sys.exit(1)
        old_manifest = json.loads(reuse_manifest_path.read_text(encoding="utf-8"))
        matched_old, _ = resolve_upscaled_sheets(
            old_manifest.get("sheets", []),
            reuse_manifest_path.parent,
            Path(args.reuse_upscaled),
            auto_match=True,
            max_mae=args.max_mae,
            verbose=True,
        )
        for entry, _ in matched_old:
            seed_by_map.setdefault(entry["map"], []).append(entry)
        print(f"Loaded {len(matched_old)} already-upscaled seed sheet(s) from {args.reuse_manifest}.")

    global_seen_hashes = set()
    manifest_sheets = []
    dedup_aliases = []
    total_sheets = 0
    maps_exported = 0
    dedup_mae = getattr(args, "dedup_mae", 0.0)
    dedup_max_diff = getattr(args, "dedup_max_diff", 25.0)

    for mdir in map_dirs:
        map_data = load_map_terrain(mdir)
        if map_data is None:
            continue
        n_dedup = deduplicate_map_tiles(
            map_data, dedup_mae, dedup_max_diff, global_seen_hashes, dedup_aliases
        )
        n_sheets, n_new_tiles = export_map_sheets(
            map_data,
            out_dir,
            args.grid,
            args.border,
            global_seen_hashes,
            manifest_sheets,
            pack_mode=args.pack,
            min_full_tiles=args.min_full_tiles,
            seed_entries=seed_by_map.get(map_data["map_name"]),
        )
        total_sheets += n_sheets
        maps_exported += 1
        dedup_note = f", dedup_aliases={n_dedup:4d}" if dedup_mae > 0.0 else ""
        print(
            f"  [{map_data['map_name']}] total={map_data['tile_count']:4d} tiles, "
            f"new_unique={n_new_tiles:4d}{dedup_note}, sheets={n_sheets:3d}"
        )

    manifest = {
        "version": 2,
        "pack_mode": args.pack,
        "grid_size": args.grid,
        "border": args.border,
        "tile_orig_size": TILE_SIZE,
        "sheet_orig_px": args.grid * TILE_SIZE,
        "maps_exported": maps_exported,
        "total_unique_tiles": len(global_seen_hashes),
        "total_sheets": total_sheets,
        "dedup_mae": dedup_mae,
        "dedup_max_diff": dedup_max_diff,
        "dedup_aliases": dedup_aliases,
        "sheets": manifest_sheets,
    }
    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        f"\nDone! Exported {total_sheets} sheet(s) ({args.grid * TILE_SIZE}x{args.grid * TILE_SIZE} px, pack={args.pack}) "
        f"covering {len(global_seen_hashes)} unique tile(s) across {maps_exported} map(s)."
    )
    print(f"Manifest saved to: {manifest_path}")


def _road_shift_or(m: "np.ndarray", r: int) -> "np.ndarray":
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy > r * r:
                continue
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def _road_components(mask: "np.ndarray"):
    h, w = mask.shape
    lab = np.zeros((h, w), dtype=np.int32)
    ys, xs = np.nonzero(mask)
    comps = []
    n = 0
    for y0, x0 in zip(ys, xs):
        if lab[y0, x0]:
            continue
        n += 1
        q = deque([(int(y0), int(x0))])
        lab[y0, x0] = n
        miny = maxy = int(y0)
        minx = maxx = int(x0)
        cnt = 0
        while q:
            y, x = q.popleft()
            cnt += 1
            if y < miny:
                miny = y
            if y > maxy:
                maxy = y
            if x < minx:
                minx = x
            if x > maxx:
                maxx = x
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = n
                        q.append((ny, nx))
        comps.append((n, cnt, maxx - minx + 1, maxy - miny + 1))
    return lab, comps


def _road_gaussian_1d(arr: "np.ndarray", sigma: float) -> "np.ndarray":
    r = max(1, int(math.ceil(3.0 * sigma)))
    x = np.arange(-r, r + 1, dtype=np.float32)
    k = np.exp(-0.5 * (x / sigma) ** 2)
    k /= k.sum()
    pad = np.pad(arr, r, mode="edge")
    return np.convolve(pad, k, mode="valid")


def _render_procedural_road_1d(
    sctx_rgb: "np.ndarray",
    mctx_bool: "np.ndarray",
    wy_world_tile: int,
    wx_world_tile: int,
    road_width_hd: float = 14.5,
    road_lanes: int = 4,
    tile_hd_px: int = 256,
):
    """
    Continuity-constrained DP centerline tracker on a 192x192 (3x3 tiles) stock context.
    Renders a crisp, anti-aliased, stock-tone-locked procedural HD highway for the center tile.
    Returns (road_rgb_tile, alpha_tile) or (None, None) if no continuous road chain is found.
    """
    h_s, w_s = mctx_bool.shape
    scale = tile_hd_px // TILE_SIZE
    h_h, w_h = h_s * scale, w_s * scale

    row_runs = []
    for y in range(h_s):
        xs = np.nonzero(mctx_bool[y])[0]
        runs = []
        if len(xs) >= 2:
            splits = np.where(np.diff(xs) > 2)[0] + 1
            for r in np.split(xs, splits):
                rw = int(r[-1] - r[0] + 1)
                if 2 <= rw <= 9:
                    runs.append((float(r.mean()) + 0.5, float(rw) * 0.5, r))
        row_runs.append(runs)

    # DP longest continuous chain across rows (bridges internal dirt-track gaps up to 18 stock px)
    best_len = {}
    prev_ptr = {}
    for y in range(h_s):
        for ri, (cx, _, _) in enumerate(row_runs[y]):
            bl = 1
            bp = None
            for py in range(max(0, y - 18), y):
                dy = y - py
                max_dx = 1.5 + 1.15 * dy
                for pri, (pcx, _, _) in enumerate(row_runs[py]):
                    if abs(cx - pcx) <= max_dx:
                        cand_l = best_len[(py, pri)] + 1
                        if cand_l > bl:
                            bl = cand_l
                            bp = (py, pri)
            best_len[(y, ri)] = bl
            prev_ptr[(y, ri)] = bp

    if not best_len:
        return None, None

    end_key = max(best_len, key=best_len.get)
    if best_len[end_key] < 8:
        return None, None

    xs_center = np.full(h_s, np.nan, dtype=np.float32)
    road_colors = np.full((h_s, 3), np.nan, dtype=np.float32)
    cur = end_key
    while cur is not None:
        y, ri = cur
        cx, _, r_idx = row_runs[y][ri]
        xs_center[y] = cx
        road_colors[y] = sctx_rgb[y, r_idx].mean(axis=0)
        cur = prev_ptr[cur]

    valid_mask = ~np.isnan(xs_center)
    if not valid_mask[60:132].any():
        return None, None

    idx_all = np.arange(h_s, dtype=np.float32)
    idx_val = np.nonzero(valid_mask)[0].astype(np.float32)
    y_first = float(idx_val[0])
    y_last = float(idx_val[-1])

    cx_interp = np.interp(idx_all, idx_val, xs_center[valid_mask])
    col_interp = np.stack(
        [np.interp(idx_all, idx_val, road_colors[valid_mask, c]) for c in range(3)],
        axis=1,
    )

    # Active strictly between chain endpoints (bridging internal crossings, stopping cleanly at bridges)
    row_active = (idx_all >= y_first - 1.5) & (idx_all <= y_last + 1.5)

    cx_smooth = _road_gaussian_1d(cx_interp, sigma=5.0)
    col_smooth = np.stack([_road_gaussian_1d(col_interp[:, c], sigma=8.0) for c in range(3)], axis=1)

    y_hd = np.linspace(0.0, h_s - 1.0, h_h, dtype=np.float32)
    cx_hd = np.interp(y_hd, idx_all, cx_smooth) * scale
    dcx_dy = np.gradient(cx_hd)
    cos_theta = 1.0 / np.sqrt(1.0 + dcx_dy * dcx_dy)
    col_hd = np.stack([np.interp(y_hd, idx_all, col_smooth[:, c]) for c in range(3)], axis=1)
    act_hd = _road_gaussian_1d(np.interp(y_hd, idx_all, row_active.astype(np.float32)), sigma=3.0)

    xx = np.arange(w_h, dtype=np.float32)[None, :] + 0.5
    d_perp = (xx - cx_hd[:, None]) * cos_theta[:, None]
    ad = np.abs(d_perp)

    # Consistent narrower asphalt width (default 14 px -> half-width 7.0 px) + soft gravel shoulder
    asphalt_hw = float(road_width_hd) * 0.5
    shoulder_hw = asphalt_hw + 4.5

    # Deterministic world-space micro-grain (100% seamless across tiles; dash period divides 256 evenly)
    wy_px = (wy_world_tile - 1) * tile_hd_px + np.arange(h_h, dtype=np.int32)[:, None]
    wx_px = (wx_world_tile - 1) * tile_hd_px + np.arange(w_h, dtype=np.int32)[None, :]
    h1 = ((wx_px * 374761393 + wy_px * 668265263) ^ ((wx_px * 1274126177) >> 13))
    h1 = ((h1 * 1274126177) >> 16) & 0xFFFF
    fine_noise = (h1.astype(np.float32) / 32767.5) - 1.0

    wy_c = wy_px // 4
    wx_c = wx_px // 2
    h2 = ((wx_c * 374761393 + wy_c * 668265263) ^ ((wy_c * 1274126177) >> 13))
    h2 = ((h2 * 1274126177) >> 16) & 0xFFFF
    coarse_noise = (h2.astype(np.float32) / 32767.5) - 1.0

    base_col = col_hd[:, None, :].copy()
    line_col = np.array([172.0, 175.0, 164.0], dtype=np.float32)[None, None, :]
    dash_phase = (wy_px % 32).astype(np.float32)

    if road_lanes == 4:
        # 4-lane divided autobahn (2+2) matching the 3D viaduct deck:
        # central dark median strip + inner barrier edges + dashed lane dividers + outer edge lines
        median_dark = np.exp(-0.5 * (ad / max(0.65, asphalt_hw * 0.11)) ** 2)
        rut1 = np.exp(-0.5 * ((ad - asphalt_hw * 0.34) / max(0.75, asphalt_hw * 0.12)) ** 2)
        rut2 = np.exp(-0.5 * ((ad - asphalt_hw * 0.74) / max(0.75, asphalt_hw * 0.12)) ** 2)
        asphalt = base_col * (0.95 - 0.18 * median_dark - 0.055 * (rut1 + rut2))[..., None]
        asphalt += (fine_noise * 2.4 + coarse_noise * 1.5)[..., None]

        line_sig = max(0.38, asphalt_hw * 0.058)
        median_edge = np.exp(-0.5 * ((ad - max(0.90, asphalt_hw * 0.14)) / line_sig) ** 2)
        dash_on = np.clip(np.minimum(dash_phase, 11.0 - dash_phase) * 0.75, 0.0, 1.0)
        lane_div = np.exp(-0.5 * ((ad - asphalt_hw * 0.54) / line_sig) ** 2) * dash_on
        outer_edge = np.exp(-0.5 * ((ad - (asphalt_hw - 0.75)) / max(0.42, line_sig)) ** 2)

        mark_strength = np.clip(
            median_edge * 0.28 + lane_div * 0.32 + outer_edge * 0.34,
            0.0,
            0.48,
        )[..., None]
        asphalt = asphalt * (1.0 - mark_strength) + line_col * mark_strength
    else:
        # 2-lane undivided highway
        rut = np.exp(-0.5 * ((ad - asphalt_hw * 0.46) / max(1.1, asphalt_hw * 0.20)) ** 2)
        asphalt = base_col * (0.93 - 0.07 * rut)[..., None]
        asphalt += (fine_noise * 2.5 + coarse_noise * 1.6)[..., None]

        line_sig = max(0.50, asphalt_hw * 0.082)
        edge_line = np.exp(-0.5 * ((ad - (asphalt_hw - 1.0)) / line_sig) ** 2)
        dash_on = np.clip(np.minimum(dash_phase, 13.0 - dash_phase) * 0.7, 0.0, 1.0)
        center_line = np.exp(-0.5 * (ad / line_sig) ** 2) * dash_on

        mark_strength = np.clip(edge_line * 0.34 + center_line * 0.34, 0.0, 0.46)[..., None]
        asphalt = asphalt * (1.0 - mark_strength) + line_col * mark_strength

    # Shoulder sampled smoothly from local stock context so it matches the surrounding biome
    s_up = Image.fromarray(np.clip(sctx_rgb, 0, 255).astype(np.uint8)).resize(
        (w_h, h_h), Image.Resampling.LANCZOS
    )
    shoulder_col = np.asarray(s_up, dtype=np.float32) * 0.96 + fine_noise[..., None] * 2.0

    t_asphalt = np.clip((asphalt_hw + 0.7 - ad) / 1.5, 0.0, 1.0)[..., None]
    road_rgb = shoulder_col * (1.0 - t_asphalt) + asphalt * t_asphalt

    t_outer = np.clip((shoulder_hw - ad) / 3.5, 0.0, 1.0)
    alpha = t_outer * act_hd[:, None]

    c0, c1 = tile_hd_px, tile_hd_px * 2
    return (
        np.clip(road_rgb[c0:c1, c0:c1], 0.0, 255.0),
        np.clip(alpha[c0:c1, c0:c1], 0.0, 1.0),
    )


def apply_road_lock_to_tiles(
    map_dir: Path,
    out_dir: Path,
    by_map: bool = False,
    road_width_hd: float = 14.5,
    road_lanes: int = 4,
    road_mask_path: str = None,
    audit_path: str = None,
):
    """Detect stock highways on the 256x256 world map and composite uniform procedural HD asphalt."""
    if np is None:
        raise RuntimeError("NumPy is required for --road-lock")

    map_data = load_map_terrain(map_dir)
    if map_data is None:
        raise FileNotFoundError(f"No terrain files (.mtd/.mtc/.mti) found in {map_dir}")

    target_dir = out_dir / map_data["map_name"] if by_map else out_dir
    pal = np.array(map_data["palette"], dtype=np.float32)
    grid = build_world_tile_grid(map_data)

    stock_rgba = {}
    for tid in range(map_data["tile_count"]):
        th = map_data["tiles_hash"][tid]
        if th not in stock_rgba:
            idx = np.frombuffer(map_data["tiles_raw"][tid], dtype=np.uint8).reshape(TILE_SIZE, TILE_SIZE)
            stock_rgba[th] = pal[idx]

    cand = {}
    for th, rgba in stock_rgba.items():
        r, g, b, a = rgba[..., 0], rgba[..., 1], rgba[..., 2], rgba[..., 3]
        lum = (r + g + b) / 3.0
        bex = b - 0.5 * (r + g)
        cand[th] = (lum >= 65.0) & (lum <= 140.0) & (bex >= -12.0) & (a >= 128.0)

    rpx = 16
    f = TILE_SIZE // rpx
    w_dim = 256 * rpx
    world = np.zeros((w_dim, w_dim), dtype=bool)
    pooled = {th: m.reshape(rpx, f, rpx, f).any(axis=(1, 3)) for th, m in cand.items()}
    for wy in range(256):
        for wx in range(256):
            tid = grid[wy][wx]
            if tid is not None:
                world[wy * rpx : (wy + 1) * rpx, wx * rpx : (wx + 1) * rpx] = pooled[map_data["tiles_hash"][tid]]

    if road_mask_path:
        with Image.open(road_mask_path) as mim:
            ext_m = np.asarray(mim.convert("L").resize((w_dim, w_dim), Image.Resampling.NEAREST)) >= 128
        keep = ext_m & _road_shift_or(world, 1)
        comp_orient = {}
        lab = np.zeros((w_dim, w_dim), dtype=np.int32)
    else:
        lab, comps = _road_components(_road_shift_or(world, 2))
        keep_ids = set()
        comp_orient = {}
        for cid, cnt, dx, dy in comps:
            ext = max(dx, dy)
            min_d = max(1, min(dx, dy))
            aspect = ext / float(min_d)
            if ext >= int(2.5 * rpx) and aspect >= 2.5:
                keep_ids.add(cid)
                comp_orient[cid] = "H" if dx > dy else "V"
        keep = np.isin(lab, list(keep_ids)) & world
        print(
            f"  [road-lock] detected {len(keep_ids)} linear road segment(s) "
            f"(filtered from {len(comps)} candidate components)"
        )

    road_mask = {}
    road_pos = {}
    road_orient = {}
    for wy in range(256):
        for wx in range(256):
            tid = grid[wy][wx]
            if tid is None:
                continue
            k = keep[wy * rpx : (wy + 1) * rpx, wx * rpx : (wx + 1) * rpx]
            if not k.any():
                continue
            th = map_data["tiles_hash"][tid]
            region = np.kron(k, np.ones((f, f), dtype=bool))
            m = cand[th] & _road_shift_or(region, 1)
            road_mask[th] = road_mask.get(th, np.zeros((TILE_SIZE, TILE_SIZE), dtype=bool)) | m
            if th not in road_pos:
                road_pos[th] = (wx, wy)
                if comp_orient:
                    sub_lab = lab[wy * rpx : (wy + 1) * rpx, wx * rpx : (wx + 1) * rpx]
                    cids = sub_lab[k]
                    cid = int(cids[0]) if len(cids) else 0
                    road_orient[th] = comp_orient.get(cid, "V")
                else:
                    road_orient[th] = "V"

    rm_full = {
        th: road_mask.get(th, np.zeros((TILE_SIZE, TILE_SIZE), dtype=bool)).astype(np.float32)
        for th in stock_rgba
    }
    stock_rgb = {th: v[..., :3] for th, v in stock_rgba.items()}

    def build_ctx(wx, wy, arr_map):
        sample = next(iter(arr_map.values()))
        ctx = np.zeros((TILE_SIZE * 3, TILE_SIZE * 3) + sample.shape[2:], dtype=np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                x = min(255, max(0, wx + dx))
                y = min(255, max(0, wy + dy))
                tid = grid[y][x]
                if tid is None:
                    continue
                ctx[(dy + 1) * TILE_SIZE : (dy + 2) * TILE_SIZE, (dx + 1) * TILE_SIZE : (dx + 2) * TILE_SIZE] = arr_map[
                    map_data["tiles_hash"][tid]
                ]
        return ctx

    audit_pairs = []
    updated = 0
    for th in sorted(road_pos.keys(), key=lambda h: (road_pos[h][1], road_pos[h][0])):
        tile_path = target_dir / f"{th}.png"
        if not tile_path.is_file():
            continue
        wx, wy = road_pos[th]
        mctx = build_ctx(wx, wy, {k: v[..., None] for k, v in rm_full.items()})[..., 0] > 0.5
        sctx = build_ctx(wx, wy, stock_rgb)

        with Image.open(tile_path) as im:
            hd_rgba = np.asarray(im.convert("RGBA"), dtype=np.float32)
        tile_hd_px = hd_rgba.shape[0]

        orient = road_orient.get(th, "V")
        if orient == "H":
            r_proc, a_proc = _render_procedural_road_1d(
                np.transpose(sctx, (1, 0, 2)),
                mctx.T,
                wx,
                wy,
                road_width_hd=road_width_hd,
                road_lanes=road_lanes,
                tile_hd_px=tile_hd_px,
            )
            if r_proc is not None:
                r_proc = np.transpose(r_proc, (1, 0, 2))
                a_proc = a_proc.T
        else:
            r_proc, a_proc = _render_procedural_road_1d(
                sctx,
                mctx,
                wy,
                wx,
                road_width_hd=road_width_hd,
                road_lanes=road_lanes,
                tile_hd_px=tile_hd_px,
            )

        if r_proc is None:
            continue

        out_rgba = hd_rgba.copy()
        out_rgba[..., :3] = hd_rgba[..., :3] * (1.0 - a_proc[..., None]) + r_proc * a_proc[..., None]
        out_u8 = np.clip(np.rint(out_rgba), 0, 255).astype(np.uint8)
        Image.fromarray(out_u8, mode="RGBA").save(tile_path)
        updated += 1

        if audit_path:
            before_im = Image.fromarray(np.clip(hd_rgba[..., :3], 0, 255).astype(np.uint8)).resize(
                (128, 128), Image.Resampling.LANCZOS
            )
            after_im = Image.fromarray(out_u8[..., :3]).resize((128, 128), Image.Resampling.LANCZOS)
            audit_pairs.append((before_im, after_im))

    print(
        f"  [road-lock] applied procedural HD road (lanes={road_lanes}, width={road_width_hd:.1f} px) "
        f"to {updated}/{len(road_pos)} unique road tile(s) in {target_dir}"
    )

    if audit_path and audit_pairs:
        cols = 4
        rows = (len(audit_pairs) + cols - 1) // cols
        cell_w = 128 * 2 + 4
        cell_h = 128 + 4
        sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), (40, 0, 40))
        for i, (b_im, a_im) in enumerate(audit_pairs):
            r, c = divmod(i, cols)
            sheet.paste(b_im, (c * cell_w, r * cell_h))
            sheet.paste(a_im, (c * cell_w + 128 + 2, r * cell_h))
        ap = Path(audit_path)
        ap.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(ap)
        print(f"  [road-lock] saved audit contact sheet ({len(audit_pairs)} tiles) to: {ap}")


def cmd_slice(args):
    manifest_path = Path(args.manifest)
    upscaled_dir = Path(args.upscaled)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    sheets = manifest.get("sheets", [])
    orig_dir = manifest_path.parent

    matched, missing = resolve_upscaled_sheets(
        sheets,
        orig_dir,
        upscaled_dir,
        getattr(args, "auto_match", False),
        args.max_mae,
        verbose=True,
    )

    # Reserve primary ownership across all available upscaled sheets so
    # primary tiles always beat shared-core fallbacks regardless of sheet order
    primary_keys = {
        (entry["map"] if args.by_map else "", t["hash"])
        for entry, _ in matched
        for t in entry["tiles"]
    }
    written_keys = set()
    written_primary = 0
    written_fallback = 0
    color_mode = getattr(args, "color_match", "hybrid")
    color_sigma = getattr(args, "color_sigma", 8.0)

    for entry, src_path in matched:
        grid_w = entry["grid_w"]
        grid_h = entry["grid_h"]

        with Image.open(src_path) as img:
            img = img.convert("RGBA")
            w, h = img.size
            if w % grid_w != 0 or h % grid_h != 0:
                print(
                    f"WARNING: Skipping {src_path.name} ({w}x{h}) — not evenly divisible by {grid_w}x{grid_h}",
                    file=sys.stderr,
                )
                continue
            tile_w = w // grid_w
            tile_h = h // grid_h
            if tile_w != tile_h:
                print(
                    f"WARNING: Non-square tile size {tile_w}x{tile_h} in {src_path.name}",
                    file=sys.stderr,
                )

            if color_mode != "none":
                img, cm_stats = apply_sheet_color_match(
                    img, orig_dir / entry["file"], entry, color_mode, color_sigma
                )
                if cm_stats:
                    print(f"  [color-lock:{color_mode}] {entry['file']} -> {cm_stats}")

            target_dir = out_dir / entry["map"] if args.by_map else out_dir
            if args.by_map:
                target_dir.mkdir(parents=True, exist_ok=True)

            map_key = entry["map"] if args.by_map else ""

            def save_tile_crop(t: dict) -> None:
                fx0 = t["col"] * tile_w
                fy0 = t["row"] * tile_h
                x0 = int(round(fx0))
                y0 = int(round(fy0))
                if abs(fx0 - x0) > 1e-3 or abs(fy0 - y0) > 1e-3:
                    print(
                        f"WARNING: Non-integer tile crop ({fx0:.2f}, {fy0:.2f}) in {src_path.name}",
                        file=sys.stderr,
                    )
                crop = img.crop((x0, y0, x0 + tile_w, y0 + tile_h))
                crop.save(target_dir / f"{t['hash']}.png")

            for t in entry["tiles"]:
                save_tile_crop(t)
                written_keys.add((map_key, t["hash"]))
                written_primary += 1

            if not getattr(args, "primary_only", False):
                for t in entry.get("core_tiles", []):
                    key = (map_key, t["hash"])
                    if key in primary_keys or key in written_keys:
                        continue
                    save_tile_crop(t)
                    written_keys.add(key)
                    written_fallback += 1

    written_aliases = 0
    for al in manifest.get("dedup_aliases", []):
        map_key = al["map"] if args.by_map else ""
        alias_key = (map_key, al["alias_hash"])
        if alias_key in written_keys:
            continue
        target_dir = out_dir / al["map"] if args.by_map else out_dir
        canon_path = target_dir / f"{al['canonical_hash']}.png"
        if not canon_path.exists():
            continue
        with Image.open(canon_path) as cim:
            rgba = np.asarray(cim.convert("RGBA"), dtype=np.float32)
            delta = al.get("mean_delta", [0.0, 0.0, 0.0])
            rgba[:, :, 0] = np.clip(rgba[:, :, 0] + float(delta[0]), 0.0, 255.0)
            rgba[:, :, 1] = np.clip(rgba[:, :, 1] + float(delta[1]), 0.0, 255.0)
            rgba[:, :, 2] = np.clip(rgba[:, :, 2] + float(delta[2]), 0.0, 255.0)
            Image.fromarray(rgba.astype(np.uint8), "RGBA").save(
                target_dir / f"{al['alias_hash']}.png"
            )
        written_keys.add(alias_key)
        written_aliases += 1

    total_written = written_primary + written_fallback + written_aliases
    alias_note = f", {written_aliases} dedup-aliases" if written_aliases else ""
    print(
        f"Done! Sliced {total_written} HD terrain tile(s) "
        f"({written_primary} primary, {written_fallback} shared-core{alias_note}) into: {out_dir}"
        + (f" (missing sheets: {missing})" if missing else "")
    )

    if getattr(args, "road_lock", False):
        if not getattr(args, "map_dir", None):
            print("ERROR: --map-dir is required when using --road-lock", file=sys.stderr)
            sys.exit(1)
        apply_road_lock_to_tiles(
            Path(args.map_dir),
            out_dir,
            by_map=args.by_map,
            road_width_hd=args.road_width,
            road_lanes=args.road_lanes,
            road_mask_path=getattr(args, "road_mask", None),
            audit_path=getattr(args, "road_audit", None),
        )


def cmd_export_tiles(args):
    map_dir = Path(args.map_dir)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    map_data = load_map_terrain(map_dir)
    if map_data is None:
        print(f"ERROR: No terrain files (.mtd/.mtc/.mti) found in {map_dir}", file=sys.stderr)
        sys.exit(1)

    seen = set()
    written = 0
    for tid in range(map_data["tile_count"]):
        th = map_data["tiles_hash"][tid]
        if th in seen:
            continue
        seen.add(th)
        img = decode_tile_image(map_data["tiles_raw"][tid], map_data["palette"])
        out_name = f"{th}.png" if args.hash_only else f"{map_data['map_name']}_t{tid:04d}_{th}.png"
        img.save(out_dir / out_name)
        written += 1

    print(f"Done! Exported {written} unique 64x64 tile(s) from {map_data['map_name']} into {out_dir}")


def main():
    parser = argparse.ArgumentParser(
        description="Ace Combat Zero — Terrain Texture Exporter & Slicer for acz_hdtex.asi"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_exp = sub.add_parser("export", help="Export seamless context-padded terrain sheets for AI upscaling")
    p_exp.add_argument("--map-dir", help="Path to a single map directory (e.g. extracted/a/data/map/m02a)")
    p_exp.add_argument("--all-maps", help="Path to extracted/a/data/map to export all maps with global deduplication")
    p_exp.add_argument("--out", required=True, help="Output directory for PNG sheets and manifest.json")
    p_exp.add_argument("--grid", type=int, default=8, help="Tiles per sheet side (default: 8 -> 512x512 -> 2048x2048 at 4x)")
    p_exp.add_argument(
        "--border",
        type=float,
        default=0.5,
        help="Seamless context border in tiles (default: 0.5 -> 32 px border, 7x7 full / 3x3 quad; 1.0 -> 6x6 full)",
    )
    p_exp.add_argument(
        "--pack",
        choices=["quad", "greedy", "grid"],
        default="quad",
        help="Sheet packing mode: quad (greedy full + 4x quad blocks, fewest sheets), greedy (full set cover), or grid (fixed stride)",
    )
    p_exp.add_argument(
        "--min-full-tiles",
        type=int,
        default=12,
        help="Minimum new unique tiles required to emit a full sheet before switching to quad blocks in --pack quad (default: 12)",
    )
    p_exp.add_argument(
        "--reuse-manifest",
        help="Optional previous manifest.json to seed already-upscaled sheets first",
    )
    p_exp.add_argument(
        "--reuse-upscaled",
        help="Directory of already-upscaled sheets matching --reuse-manifest",
    )
    p_exp.add_argument(
        "--max-mae",
        type=float,
        default=20.0,
        help="Max RGB MAE when auto-matching --reuse-upscaled (default: 20.0)",
    )
    p_exp.add_argument(
        "--dedup-mae",
        type=float,
        default=0.0,
        help="Near-duplicate tile deduplication RGB MAE threshold on 16x16 blocks (0=off, recommended: 2.5)",
    )
    p_exp.add_argument(
        "--dedup-max-diff",
        type=float,
        default=25.0,
        help="Max single-block RGB diff allowed when deduplicating near-identical tiles (default: 25.0)",
    )
    p_exp.set_defaults(func=cmd_export)

    p_slc = sub.add_parser("slice", help="Slice upscaled terrain sheets back into <HASH>.png HD tiles")
    p_slc.add_argument("--manifest", required=True, help="Path to manifest.json produced by export")
    p_slc.add_argument("--upscaled", required=True, help="Directory containing upscaled sheet PNGs")
    p_slc.add_argument("--out", required=True, help="Output directory (e.g. mods/textures/terrain)")
    p_slc.add_argument("--by-map", action="store_true", help="Organize sliced tiles into <out>/<map>/<HASH>.png subfolders")
    p_slc.add_argument(
        "--auto-match",
        action="store_true",
        help="Automatically match arbitrarily named upscaled images (e.g. Nano Banana *_2K.jpg) against original sheets next to manifest.json by visual similarity",
    )
    p_slc.add_argument(
        "--max-mae",
        type=float,
        default=20.0,
        help="Maximum allowed RGB MAE (0..255) for --auto-match (default: 20.0)",
    )
    p_slc.add_argument(
        "--primary-only",
        action="store_true",
        help="Only slice each sheet's globally primary claimed tiles (skip shared core_tiles from un-upscaled earlier sheets)",
    )
    p_slc.add_argument(
        "--color-match",
        choices=["hybrid", "affine", "none"],
        default="hybrid",
        help="Color-lock upscaled sheets to original 512x512 sheet palette/tone (default: hybrid)",
    )
    p_slc.add_argument(
        "--color-sigma",
        type=float,
        default=8.0,
        help="Low-frequency Gaussian sigma in original-sheet pixels for hybrid color-match (default: 8.0)",
    )
    p_slc.add_argument(
        "--road-lock",
        action="store_true",
        help="Post-process detected stock highways with uniform procedural HD asphalt (requires --map-dir)",
    )
    p_slc.add_argument(
        "--map-dir",
        help="Path to extracted map directory (e.g. extracted/a/data/map/m02a) for --road-lock",
    )
    p_slc.add_argument(
        "--road-width",
        type=float,
        default=14.5,
        help="Procedural HD asphalt width in 256px-tile pixels for --road-lock (default: 14.5)",
    )
    p_slc.add_argument(
        "--road-lanes",
        type=int,
        choices=[2, 4],
        default=4,
        help="Procedural HD highway lane layout for --road-lock: 4 (2+2 divided autobahn, default) or 2 (undivided)",
    )
    p_slc.add_argument(
        "--road-mask",
        help="Optional external binary world road mask PNG to override automatic road detection",
    )
    p_slc.add_argument(
        "--road-audit",
        help="Optional output PNG path for a before/after contact sheet of all road-locked tiles",
    )
    p_slc.set_defaults(func=cmd_slice)

    p_raw = sub.add_parser("export-tiles", help="Export individual unique 64x64 tiles directly")
    p_raw.add_argument("--map-dir", required=True, help="Path to a single map directory")
    p_raw.add_argument("--out", required=True, help="Output directory for 64x64 PNG tiles")
    p_raw.add_argument("--hash-only", action="store_true", help="Name files <HASH>.png directly")
    p_raw.set_defaults(func=cmd_export_tiles)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
