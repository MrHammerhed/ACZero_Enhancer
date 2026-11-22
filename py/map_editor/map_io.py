#!/usr/bin/env python3
"""
Ace Combat Zero: The Belkan War — Map Binary I/O & Geometry Decoder
===================================================================
Parses and serializes ACZ gameplay map layers:
  - Heightmap:     <map>.mha (16x16 uint8), <map>.mhd (N x 33x33 uint8)
  - Terrain:       <map>.mta, .mti, .mtd, .mtc (+ optional HD tiles from mods/textures/terrain)
  - Placed Objects:<map>.pdl (instances), <map>.pml (cell lists), <map>.pmi (256x256 uint16 grid)
  - Forests:       <map>.efa (128x128 uint8 mask)
  - Models:        pmmdl<map>.bin (ACM v5 VIF UNPACK V3-32 triangle strips & bounding boxes)

Non-destructive editing workflow:
  - Reads vanilla files from extracted/a/data/map/<map>/
  - Overlays any existing modified files from mods/maps/<map>/
  - Saves edits exclusively to mods/maps/<map>/ (ready for direct .asi file-override hook)
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np
from PIL import Image

# World coordinate constants (verified across all ACZ maps)
WORLD_MIN = -262144.0
WORLD_MAX = 262144.0
WORLD_SIZE = 524288.0          # 256 tiles * 2048 units/tile
TILE_WORLD_SIZE = 2048.0       # 1 MTI/PMI cell = 2048 x 2048 world units
SECTOR_WORLD_SIZE = 32768.0    # 16 tiles = 32768 world units
GRID_TILES = 256               # 256 x 256 PMI / MTI grid
GRID_SECTORS = 16              # 16 x 16 MHA / MTA grid
HEIGHT_SECTOR_DIM = 33         # 33 x 33 vertices per height sector
HEIGHT_SECTOR_BYTES = 1089     # 33 * 33
HEIGHT_FULL_DIM = 513          # 16 * 32 + 1 vertices across full map
EFA_DIM = 128                  # 128 x 128 forest grid
EFA_BYTES = 16384              # 128 * 128
PML_SLOTS = 8                  # Max 8 PDL indices per PMI cell
DEFAULT_HEIGHT_SCALE = 85.33333333333333  # ~2048 / 24

# FNV-1a 64-bit (matches acz_hdtex.asi & ACZ_Terrain_Converter.py)
FNV1A_64_OFFSET = 0xCBF29CE484222325
FNV1A_64_PRIME = 0x00000100000001B3
FNV1A_64_MASK = 0xFFFFFFFFFFFFFFFF


def fnv1a_64(data: bytes) -> int:
    h = FNV1A_64_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV1A_64_PRIME) & FNV1A_64_MASK
    return h


def unswizzle_csm1(clut_bytes: bytes) -> np.ndarray:
    """Unswizzle PS2 GS CSM1 256-color RGBA32 CLUT -> uint8 array (256, 4) with opaque alpha."""
    if len(clut_bytes) < 1024:
        raise ValueError(f"MTC palette too small: {len(clut_bytes)} bytes")
    lut = np.zeros((256, 4), dtype=np.uint8)
    for i in range(256):
        block = i // 32
        entry = i % 32
        if 8 <= entry < 16:
            orig_i = block * 32 + entry + 8
        elif 16 <= entry < 24:
            orig_i = block * 32 + entry - 8
        else:
            orig_i = i
        lut[i, 0] = clut_bytes[orig_i * 4 + 0]
        lut[i, 1] = clut_bytes[orig_i * 4 + 1]
        lut[i, 2] = clut_bytes[orig_i * 4 + 2]
        lut[i, 3] = 255
    return lut


# ============================================================================
# 1. Heightmap (.mha + .mhd)
# ============================================================================

@dataclass
class HeightmapData:
    mha: bytes                # 256 bytes (16x16 uint8)
    mhd: bytes                # N * 1089 bytes
    grid_raw: np.ndarray      # (513, 513) float32 raw [0..255]
    height_scale: float = DEFAULT_HEIGHT_SCALE

    def sample_raw(self, x: float, z: float) -> float:
        """Bilinearly sample raw height [0..255] at world coordinates (x, z)."""
        gx = (x - WORLD_MIN) / WORLD_SIZE * (HEIGHT_FULL_DIM - 1)
        gz = (z - WORLD_MIN) / WORLD_SIZE * (HEIGHT_FULL_DIM - 1)
        gx = max(0.0, min(float(HEIGHT_FULL_DIM - 1), gx))
        gz = max(0.0, min(float(HEIGHT_FULL_DIM - 1), gz))
        ix = int(gx)
        iz = int(gz)
        ix1 = min(HEIGHT_FULL_DIM - 1, ix + 1)
        iz1 = min(HEIGHT_FULL_DIM - 1, iz + 1)
        tx = gx - ix
        tz = gz - iz
        h00 = float(self.grid_raw[iz, ix])
        h10 = float(self.grid_raw[iz, ix1])
        h01 = float(self.grid_raw[iz1, ix])
        h11 = float(self.grid_raw[iz1, ix1])
        return (
            h00 * (1.0 - tx) * (1.0 - tz)
            + h10 * tx * (1.0 - tz)
            + h01 * (1.0 - tx) * tz
            + h11 * tx * tz
        )

    def sample_world_y(self, x: float, z: float) -> float:
        return self.sample_raw(x, z) * self.height_scale

    def grid_world_f32(self, downsample: int = 1) -> np.ndarray:
        """Return world-space heightmap as contiguous float32 array (H, W)."""
        g = self.grid_raw * float(self.height_scale)
        if downsample > 1:
            g = g[::downsample, ::downsample]
        return np.ascontiguousarray(g, dtype=np.float32)


def load_heightmap(map_dir: Path, map_name: str) -> Optional[HeightmapData]:
    mha_path = map_dir / f"{map_name}.mha"
    mhd_path = map_dir / f"{map_name}.mhd"
    if not (mha_path.is_file() and mhd_path.is_file()):
        return None

    mha = mha_path.read_bytes()
    mhd = mhd_path.read_bytes()
    if len(mha) < 256:
        raise ValueError(f"{mha_path.name} too small: {len(mha)} bytes")

    n_sectors = len(mhd) // HEIGHT_SECTOR_BYTES
    sectors = np.frombuffer(
        mhd[: n_sectors * HEIGHT_SECTOR_BYTES], dtype=np.uint8
    ).reshape((n_sectors, HEIGHT_SECTOR_DIM, HEIGHT_SECTOR_DIM))

    grid = np.zeros((HEIGHT_FULL_DIM, HEIGHT_FULL_DIM), dtype=np.float32)
    for sz in range(GRID_SECTORS):
        for sx in range(GRID_SECTORS):
            sec_idx = mha[sz * GRID_SECTORS + sx]
            if sec_idx < n_sectors:
                y0 = sz * 32
                x0 = sx * 32
                grid[y0 : y0 + HEIGHT_SECTOR_DIM, x0 : x0 + HEIGHT_SECTOR_DIM] = sectors[sec_idx]

    return HeightmapData(mha=mha, mhd=mhd, grid_raw=grid)


def calibrate_height_scale(heightmap: Optional[HeightmapData], objects: List[dict]) -> float:
    """Estimate world Y / raw height ratio from placed objects on non-zero terrain."""
    if heightmap is None or not objects:
        return DEFAULT_HEIGHT_SCALE
    ratios = []
    for obj in objects:
        y = obj["y"]
        if y <= 1.0:
            continue
        h_raw = heightmap.sample_raw(obj["x"], obj["z"])
        if h_raw > 1.0:
            r = y / h_raw
            if 5.0 <= r <= 500.0:
                ratios.append(r)
    if not ratios:
        return DEFAULT_HEIGHT_SCALE
    ratios.sort()
    return float(ratios[len(ratios) // 2])


# ============================================================================
# 2. Placed Objects (.pdl, .pml, .pmi)
# ============================================================================

def parse_pdl(data: bytes) -> List[dict]:
    """Parse <map>.pdl into list of {id, x, y, z, model_id}."""
    if len(data) < 4:
        return []
    count = struct.unpack_from("<I", data, 0)[0]
    if count == 0 or len(data) < 16 + count * 16:
        return []
    objs = []
    for i in range(count):
        x, y, z, model_id = struct.unpack_from("<3fI", data, 16 + i * 16)
        objs.append({
            "id": i,
            "x": float(x),
            "y": float(y),
            "z": float(z),
            "model_id": int(model_id),
        })
    return objs


def parse_pml(data: bytes) -> List[Tuple[int, ...]]:
    """Parse <map>.pml into list of 8-int16 tuples."""
    if len(data) < 16:
        return []
    count = len(data) // 16
    return [struct.unpack_from("<8h", data, i * 16) for i in range(count)]


def parse_pmi(data: bytes) -> np.ndarray:
    """Parse <map>.pmi into (256, 256) uint16 array (supports 4-byte empty stub)."""
    if len(data) < GRID_TILES * GRID_TILES * 2:
        return np.zeros((GRID_TILES, GRID_TILES), dtype=np.uint16)
    return np.frombuffer(data[: GRID_TILES * GRID_TILES * 2], dtype=np.uint16).reshape((GRID_TILES, GRID_TILES)).copy()


def world_to_tile(x: float, z: float) -> Tuple[int, int]:
    gx = int(math.floor((x - WORLD_MIN) / TILE_WORLD_SIZE))
    gz = int(math.floor((z - WORLD_MIN) / TILE_WORLD_SIZE))
    gx = max(0, min(GRID_TILES - 1, gx))
    gz = max(0, min(GRID_TILES - 1, gz))
    return gx, gz


def serialize_placed_objects(
    objects: List[dict],
    models: Optional[List[dict]] = None,
) -> Tuple[bytes, bytes, bytes, List[str]]:
    """
    Serialize list of object dicts [{x, y, z, model_id}] into (pdl_bytes, pml_bytes, pmi_bytes, warnings).
    Rebuilds the 256x256 spatial lookup grid (.pmi) and deduplicated cell list (.pml).
    When `models` is provided, each object is registered in every PMI cell overlapped by its
    world-space bounding box (matching ACZ's culling/collision grid for bridges & large structures).
    """
    warnings: List[str] = []
    count = len(objects)

    if count == 0:
        pdl_bytes = struct.pack("<I", 0)
        pml_bytes = struct.pack("<I", 0)
        pmi_bytes = struct.pack("<I", 0)
        return pdl_bytes, pml_bytes, pmi_bytes, warnings

    if count > 32767:
        warnings.append(f"Object count ({count}) exceeds int16 limit (32767) for .pml indices; truncating.")
        objects = objects[:32767]
        count = len(objects)

    model_bboxes: Dict[int, Tuple[float, float, float, float]] = {}
    if models:
        for m in models:
            bb = m.get("bbox")
            if bb and len(bb) == 6:
                model_bboxes[int(m["model_id"])] = (float(bb[0]), float(bb[2]), float(bb[3]), float(bb[5]))

    # 1. Build .pdl
    pdl_buf = bytearray(16 + count * 16)
    struct.pack_into("<I", pdl_buf, 0, count)
    cell_buckets: Dict[Tuple[int, int], List[int]] = {}

    for idx, obj in enumerate(objects):
        x = float(obj["x"])
        y = float(obj["y"])
        z = float(obj["z"])
        model_id = int(obj["model_id"])
        struct.pack_into("<3fI", pdl_buf, 16 + idx * 16, x, y, z, model_id)
        bb = model_bboxes.get(model_id)
        if bb is not None:
            min_x, min_z, max_x, max_z = bb
            gx0, gz0 = world_to_tile(x + min_x, z + min_z)
            gx1, gz1 = world_to_tile(x + max_x, z + max_z)
            # Clamp huge outlier bboxes to a sane radius around center
            if (gx1 - gx0) > 8 or (gz1 - gz0) > 8:
                gx0, gz0 = world_to_tile(x, z)
                gx1, gz1 = gx0, gz0
        else:
            gx0, gz0 = world_to_tile(x, z)
            gx1, gz1 = gx0, gz0

        for gz in range(gz0, gz1 + 1):
            for gx in range(gx0, gx1 + 1):
                cell_buckets.setdefault((gx, gz), []).append(idx)

    # 2. Build .pml and .pmi (row-major scan of cells so ordering is deterministic)
    pml_entries: List[Tuple[int, ...]] = [(-1,) * PML_SLOTS]  # pml[0] = empty cell
    pml_lookup: Dict[Tuple[int, ...], int] = {pml_entries[0]: 0}
    pmi_grid = np.zeros((GRID_TILES, GRID_TILES), dtype=np.uint16)

    for (gx, gz) in sorted(cell_buckets.keys(), key=lambda c: (c[1], c[0])):
        indices = cell_buckets[(gx, gz)]
        if len(indices) > PML_SLOTS:
            warnings.append(
                f"Tile ({gx}, {gz}) has {len(indices)} objects (max {PML_SLOTS}); "
                f"dropping {len(indices) - PML_SLOTS} overflow object(s) from PMI cell."
            )
            indices = indices[:PML_SLOTS]
        padded = tuple(indices) + (-1,) * (PML_SLOTS - len(indices))
        pml_idx = pml_lookup.get(padded)
        if pml_idx is None:
            pml_idx = len(pml_entries)
            if pml_idx > 65535:
                warnings.append("PML entry count exceeded uint16 limit (65535).")
                break
            pml_entries.append(padded)
            pml_lookup[padded] = pml_idx
        pmi_grid[gz, gx] = pml_idx

    pml_buf = bytearray(len(pml_entries) * 16)
    for i, entry in enumerate(pml_entries):
        struct.pack_into("<8h", pml_buf, i * 16, *entry)

    return bytes(pdl_buf), bytes(pml_buf), pmi_grid.tobytes(), warnings


# ============================================================================
# 3. Forest Mask (.efa)
# ============================================================================

def parse_efa(data: bytes) -> np.ndarray:
    """Parse <map>.efa into (128, 128) uint8 array (0xFF = empty, 0x00 = forest)."""
    if len(data) != EFA_BYTES:
        arr = np.full((EFA_DIM, EFA_DIM), 255, dtype=np.uint8)
        n = min(len(data), EFA_BYTES)
        arr.flat[:n] = np.frombuffer(data[:n], dtype=np.uint8)
        return arr
    return np.frombuffer(data, dtype=np.uint8).reshape((EFA_DIM, EFA_DIM)).copy()


def serialize_efa(grid: np.ndarray) -> bytes:
    arr = np.asarray(grid, dtype=np.uint8).reshape((EFA_DIM, EFA_DIM))
    return arr.tobytes()


# ============================================================================
# 4. ACM v5 3D Model Decoder (pmmdl<map>.bin)
# ============================================================================

def decode_acm_geometry(chunk: bytes) -> dict:
    """
    Decode PS2 VIF UNPACK V3-32 vertex strips from an ACM\\x00 chunk inside pmmdl<map>.bin.
    Returns {
        "radius": float,
        "height": float,
        "bbox": [minX, minY, minZ, maxX, maxY, maxZ],
        "positions": [x0, y0, z0, ...],  # flat triangle list
        "tri_count": int,
    }
    """
    if len(chunk) < 32 or chunk[:4] != b"ACM\x00":
        return {"radius": 100.0, "height": 50.0, "bbox": [-50, 0, -50, 50, 50, 50], "positions": [], "tri_count": 0}

    radius, height = struct.unpack_from("<2f", chunk, 8)
    # Scan for PS2 VIF UNPACK V3-32 tags:
    #   word layout: cmd=0x68 (V3-32) in bits 24..31, num in bits 16..23, addr in bits 0..15.
    #   In ACZ ACM files, vertex position packets consistently use (word & 0xFF00FFFF) == 0x68008001
    #   with 3 <= num <= 64.
    positions: List[float] = []
    min_x = min_y = min_z = float("inf")
    max_x = max_y = max_z = float("-inf")

    n_words = len(chunk) // 4
    words = struct.unpack_from(f"<{n_words}I", chunk, 0)
    i = 16  # skip ACM header
    while i < n_words:
        w = words[i]
        if (w & 0xFF00FFFF) == 0x68008001:
            num = (w >> 16) & 0xFF
            if 3 <= num <= 80 and (i + 1 + num * 3) <= n_words:
                verts = struct.unpack_from(f"<{num * 3}f", chunk, (i + 1) * 4)
                # Sanity check coordinates (reject NaN/inf/absurd values)
                valid = True
                for v in verts:
                    if not math.isfinite(v) or abs(v) > 200000.0:
                        valid = False
                        break
                if valid:
                    for k in range(num):
                        vx, vy, vz = verts[k * 3], verts[k * 3 + 1], verts[k * 3 + 2]
                        if vx < min_x: min_x = vx
                        if vy < min_y: min_y = vy
                        if vz < min_z: min_z = vz
                        if vx > max_x: max_x = vx
                        if vy > max_y: max_y = vy
                        if vz > max_z: max_z = vz
                    # Convert triangle strip to triangle list
                    for k in range(num - 2):
                        if k % 2 == 0:
                            i0, i1, i2 = k, k + 1, k + 2
                        else:
                            i0, i1, i2 = k, k + 2, k + 1
                        positions.extend((
                            verts[i0 * 3], verts[i0 * 3 + 1], verts[i0 * 3 + 2],
                            verts[i1 * 3], verts[i1 * 3 + 1], verts[i1 * 3 + 2],
                            verts[i2 * 3], verts[i2 * 3 + 1], verts[i2 * 3 + 2],
                        ))
                    i += 1 + num * 3
                    continue
        i += 1

    if not math.isfinite(min_x):
        r = radius if (math.isfinite(radius) and 1.0 < radius < 50000.0) else 100.0
        h = height if (math.isfinite(height) and 1.0 < height < 50000.0) else 60.0
        bbox = [-r * 0.5, 0.0, -r * 0.5, r * 0.5, h, r * 0.5]
    else:
        bbox = [round(min_x, 2), round(min_y, 2), round(min_z, 2), round(max_x, 2), round(max_y, 2), round(max_z, 2)]

    # Round positions for compact JSON transfer
    positions_rounded = [round(v, 2) for v in positions]
    return {
        "radius": round(radius, 2) if math.isfinite(radius) else 0.0,
        "height": round(height, 2) if math.isfinite(height) else 0.0,
        "bbox": bbox,
        "positions": positions_rounded,
        "tri_count": len(positions_rounded) // 9,
    }


def load_pmmdl_catalog(map_dir: Path, map_name: str) -> List[dict]:
    """Load all models from pmmdl<map>.bin with decoded geometry & bounding boxes."""
    pmmdl_path = map_dir / f"pmmdl{map_name}.bin"
    if not pmmdl_path.is_file():
        return []
    data = pmmdl_path.read_bytes()
    if len(data) < 8:
        return []
    count = struct.unpack_from("<I", data, 0)[0]
    if count == 0 or len(data) < 4 + (count + 1) * 4:
        return []
    offs = struct.unpack_from(f"<{count + 1}I", data, 4)
    models = []
    for idx in range(count):
        start = offs[idx]
        end = offs[idx + 1] if offs[idx + 1] > start else len(data)
        chunk = data[start:end]
        geo = decode_acm_geometry(chunk)
        geo["model_id"] = idx
        geo["byte_size"] = len(chunk)
        models.append(geo)
    return models


# ============================================================================
# 5. Terrain Texture Compositor (Vanilla + HD overrides)
# ============================================================================

def _find_hd_tile_path(hd_terrain_dir: Optional[Path], map_name: str, h_hex: str) -> Optional[Path]:
    if hd_terrain_dir is None or not hd_terrain_dir.is_dir():
        return None
    for base in (hd_terrain_dir / map_name, hd_terrain_dir):
        for ext in (".png", ".jpg", ".jpeg", ".webp"):
            candidate = base / f"{h_hex}{ext}"
            if candidate.is_file():
                return candidate
    return None


def render_terrain_overview(
    map_dir: Path,
    map_name: str,
    hd_terrain_dir: Optional[Path] = None,
    use_hd: bool = True,
    out_size: int = 2048,
) -> Optional[Image.Image]:
    """
    Compose the full 256x256 terrain map into a single overview RGB image of size (out_size, out_size).
    When use_hd=True and hd_terrain_dir contains <FNV1A_HASH>.png tiles (either directly or under <map_name>/),
    uses HD tiles automatically.
    """
    mtd_path = map_dir / f"{map_name}.mtd"
    mtc_path = map_dir / f"{map_name}.mtc"
    mti_path = map_dir / f"{map_name}.mti"
    mta_path = map_dir / f"{map_name}.mta"

    if not (mtd_path.is_file() and mtc_path.is_file() and mti_path.is_file()):
        return None

    mtd = mtd_path.read_bytes()
    mtc = mtc_path.read_bytes()
    mti = mti_path.read_bytes()
    mta = mta_path.read_bytes() if mta_path.is_file() else None

    tile_count, data_offset, _ = struct.unpack_from("<III", mtd, 0)
    lut = unswizzle_csm1(mtc)  # (256, 4)
    mti_u16 = struct.unpack(f"<{len(mti) // 2}H", mti)

    tile_px = max(4, out_size // GRID_TILES)
    canvas_size = tile_px * GRID_TILES

    # Decode each unique tile once at target resolution (tile_px x tile_px)
    decoded_tiles: List[np.ndarray] = []
    for tid in range(tile_count):
        off = data_offset + tid * 4096
        raw = mtd[off : off + 4096]
        tile_img: Optional[Image.Image] = None

        if use_hd:
            hd_path = _find_hd_tile_path(hd_terrain_dir, map_name, f"{fnv1a_64(raw):016X}")
            if hd_path is not None:
                try:
                    with Image.open(hd_path) as im:
                        tile_img = im.convert("RGB").resize((tile_px, tile_px), Image.Resampling.BILINEAR)
                except Exception:
                    tile_img = None

        if tile_img is None:
            idx_arr = np.frombuffer(raw, dtype=np.uint8).reshape((64, 64))
            rgb_arr = lut[idx_arr, :3]
            tile_img = Image.fromarray(rgb_arr, mode="RGB").resize((tile_px, tile_px), Image.Resampling.BILINEAR)

        decoded_tiles.append(np.asarray(tile_img, dtype=np.uint8))

    # Assemble 16x16 sectors (each 16x16 tiles) using MTA sector indirection
    canvas = np.zeros((canvas_size, canvas_size, 3), dtype=np.uint8)
    n_mti_sectors = len(mti_u16) // 256

    for sz in range(GRID_SECTORS):
        for sx in range(GRID_SECTORS):
            if mta is not None and len(mta) >= 256:
                sec_id = mta[sz * GRID_SECTORS + sx]
            else:
                sec_id = sz * GRID_SECTORS + sx
            if sec_id >= n_mti_sectors:
                continue
            sec_base = sec_id * 256
            for lz in range(16):
                gz = sz * 16 + lz
                py0 = gz * tile_px
                for lx in range(16):
                    gx = sx * 16 + lx
                    px0 = gx * tile_px
                    entry = mti_u16[sec_base + lz * 16 + lx]
                    tid = entry & 0x03FF
                    if tid < tile_count:
                        canvas[py0 : py0 + tile_px, px0 : px0 + tile_px] = decoded_tiles[tid]

    return Image.fromarray(canvas, mode="RGB")


def build_terrain_atlas_and_tilemap(
    map_dir: Path,
    map_name: str,
    hd_terrain_dir: Optional[Path] = None,
    use_hd: bool = True,
    tile_px: int = 128,
) -> Optional[Tuple[Image.Image, int, int, int, np.ndarray]]:
    """
    Build a high-resolution tile atlas (cols x cols grid of tile_px x tile_px unique tiles)
    plus the 256x256 uint16 tile index map (after MTA sector indirection).
    Returns (atlas_rgb_image, cols, tile_count, actual_tile_px, tilemap_u16_256x256).
    """
    mtd_path = map_dir / f"{map_name}.mtd"
    mtc_path = map_dir / f"{map_name}.mtc"
    mti_path = map_dir / f"{map_name}.mti"
    mta_path = map_dir / f"{map_name}.mta"

    if not (mtd_path.is_file() and mtc_path.is_file() and mti_path.is_file()):
        return None

    mtd = mtd_path.read_bytes()
    mtc = mtc_path.read_bytes()
    mti = mti_path.read_bytes()
    mta = mta_path.read_bytes() if mta_path.is_file() else None

    tile_count, data_offset, _ = struct.unpack_from("<III", mtd, 0)
    lut = unswizzle_csm1(mtc)
    mti_u16 = struct.unpack(f"<{len(mti) // 2}H", mti)

    cols = max(1, int(math.ceil(math.sqrt(max(1, tile_count)))))
    # Keep atlas <= 4096px for universal WebGL2 GPU compatibility
    while cols * tile_px > 4096 and tile_px > 32:
        tile_px //= 2
    atlas_size = cols * tile_px
    atlas = np.zeros((atlas_size, atlas_size, 3), dtype=np.uint8)

    for tid in range(tile_count):
        off = data_offset + tid * 4096
        raw = mtd[off : off + 4096]
        tile_img: Optional[Image.Image] = None

        if use_hd:
            hd_path = _find_hd_tile_path(hd_terrain_dir, map_name, f"{fnv1a_64(raw):016X}")
            if hd_path is not None:
                try:
                    with Image.open(hd_path) as im:
                        tile_img = im.convert("RGB").resize((tile_px, tile_px), Image.Resampling.BILINEAR)
                except Exception:
                    tile_img = None

        if tile_img is None:
            idx_arr = np.frombuffer(raw, dtype=np.uint8).reshape((64, 64))
            rgb_arr = lut[idx_arr, :3]
            tile_img = Image.fromarray(rgb_arr, mode="RGB").resize((tile_px, tile_px), Image.Resampling.BILINEAR)

        col = tid % cols
        row = tid // cols
        atlas[row * tile_px : (row + 1) * tile_px, col * tile_px : (col + 1) * tile_px] = np.asarray(
            tile_img, dtype=np.uint8
        )

    tilemap = np.zeros((GRID_TILES, GRID_TILES), dtype=np.uint16)
    n_mti_sectors = len(mti_u16) // 256
    for sz in range(GRID_SECTORS):
        for sx in range(GRID_SECTORS):
            sec_id = mta[sz * GRID_SECTORS + sx] if (mta is not None and len(mta) >= 256) else (sz * GRID_SECTORS + sx)
            if sec_id >= n_mti_sectors:
                continue
            sec_base = sec_id * 256
            for lz in range(16):
                gz = sz * 16 + lz
                for lx in range(16):
                    gx = sx * 16 + lx
                    tid = mti_u16[sec_base + lz * 16 + lx] & 0x03FF
                    tilemap[gz, gx] = tid if tid < tile_count else 0

    return Image.fromarray(atlas, mode="RGB"), cols, tile_count, tile_px, tilemap


def count_hd_tiles_for_map(map_dir: Path, map_name: str, hd_terrain_dir: Optional[Path]) -> Tuple[int, int]:
    """Return (hd_found, total_unique_tiles) for a map."""
    mtd_path = map_dir / f"{map_name}.mtd"
    if not mtd_path.is_file():
        return 0, 0
    mtd = mtd_path.read_bytes()
    tile_count, data_offset, _ = struct.unpack_from("<III", mtd, 0)
    if hd_terrain_dir is None or not hd_terrain_dir.is_dir():
        return 0, tile_count
    found = 0
    for tid in range(tile_count):
        off = data_offset + tid * 4096
        raw = mtd[off : off + 4096]
        if _find_hd_tile_path(hd_terrain_dir, map_name, f"{fnv1a_64(raw):016X}") is not None:
            found += 1
    return found, tile_count


# ============================================================================
# 6. High-Level Map Bundle Loader / Saver
# ============================================================================

@dataclass
class MapBundle:
    map_name: str
    vanilla_dir: Path
    override_dir: Path
    heightmap: Optional[HeightmapData]
    objects: List[dict]
    efa: np.ndarray
    models: List[dict]
    height_scale: float
    has_overrides: bool = False


def resolve_file(vanilla_dir: Path, override_dir: Path, filename: str) -> Tuple[Path, bool]:
    ovr = override_dir / filename
    if ovr.is_file():
        return ovr, True
    return vanilla_dir / filename, False


def load_map_bundle(vanilla_dir: Path, override_dir: Path, map_name: str) -> MapBundle:
    has_ovr = False

    hm = load_heightmap(vanilla_dir, map_name)

    pdl_path, is_ovr = resolve_file(vanilla_dir, override_dir, f"{map_name}.pdl")
    has_ovr = has_ovr or is_ovr
    objects = parse_pdl(pdl_path.read_bytes()) if pdl_path.is_file() else []

    efa_path, is_ovr = resolve_file(vanilla_dir, override_dir, f"{map_name}.efa")
    has_ovr = has_ovr or is_ovr
    efa = parse_efa(efa_path.read_bytes()) if efa_path.is_file() else np.full((EFA_DIM, EFA_DIM), 255, dtype=np.uint8)

    models = load_pmmdl_catalog(vanilla_dir, map_name)
    h_scale = calibrate_height_scale(hm, objects)
    if hm is not None:
        hm.height_scale = h_scale

    return MapBundle(
        map_name=map_name,
        vanilla_dir=vanilla_dir,
        override_dir=override_dir,
        heightmap=hm,
        objects=objects,
        efa=efa,
        models=models,
        height_scale=h_scale,
        has_overrides=has_ovr,
    )


def save_map_overrides(
    override_dir: Path,
    map_name: str,
    objects: List[dict],
    efa: np.ndarray,
    models: Optional[List[dict]] = None,
) -> dict:
    """
    Write modified .pdl, .pml, .pmi, .efa to mods/maps/<map_name>/ (non-destructive).
    Returns summary dict with written file paths and any warnings.
    """
    override_dir.mkdir(parents=True, exist_ok=True)
    pdl_bytes, pml_bytes, pmi_bytes, warnings = serialize_placed_objects(objects, models=models)
    efa_bytes = serialize_efa(efa)

    written = {}
    for ext, payload in (
        ("pdl", pdl_bytes),
        ("pml", pml_bytes),
        ("pmi", pmi_bytes),
        ("efa", efa_bytes),
    ):
        out_path = override_dir / f"{map_name}.{ext}"
        out_path.write_bytes(payload)
        written[ext] = {"path": str(out_path), "bytes": len(payload)}

    return {
        "map_name": map_name,
        "override_dir": str(override_dir),
        "files": written,
        "object_count": len(objects),
        "forest_cells": int(np.count_nonzero(efa != 255)),
        "warnings": warnings,
    }
