#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ace Combat Zero: The Belkan War (PC / PS2) — Universal .FHM & Font Converter (v5)
=================================================================================
Supports all .FHM container types found in the game's archives:
  1. Multi-level .FHM containers with embedded GIM textures and bitmap font metric
     tables (e.g. debrief/opm_fontdata.fhm, hud/huddataN.fhm):
     - Recursively unpacks the entire hierarchy of nested .FHM containers.
     - Extracts all embedded GIM\\0 textures into both .gim and .png (4-bit PSMT4
       and 8-bit PSMT8 with accurate PS2 alpha 0x80 <-> 255 scaling and CSM1 palette
       unswizzling).
     - Computes the exact 64-bit FNV-1a hash of each GIM texture (with byte 0x12
       and PSM 0x1B->0x13 normalization) for direct HD replacement via acz_hdtex.dll
       (mods/textures/<HASH>.png).
     - Decodes embedded UV coordinate and character width tables for bitmap fonts
       into human-readable JSON (*_font_metric.json).
  2. Dynamic tile font and text packages for pause menus, briefings, and UI
     (e.g. pausetxt/ntsc/*Pmenu.fhm, select/pause_data_*.fhm — parsed at 0x1401FFBD0
     in acz.exe, both 32x32 log2=5 and 16x16 log2=4):
     - Extracts 4-bit PSMT4 glyph tiles into an RGBA atlas *_glyphs_<HASH>.png,
       ready for direct HD replacement via acz_hdtex.dll (mods/textures/<HASH>.png).
     - Computes the exact 64-bit FNV-1a hash of the dynamic font (with runtime_tbp
       and flags normalized), matching acz_hdtex.dll.
     - Decodes the glyph table (UTF-16LE character, advance_x, height) and text
       strings across all 7 language slots (0=JP, 1=EN_US, 2=EN_UK, 3=IT, 4=DE,
       5=FR, 6=ES) into readable JSON (*_pmenu_font.json).
  3. Full Repack from FHM_UNPACKED to FHM_OUT:
     - Rebuilds modified PNGs back into GIM textures or Pmenu glyph tables (with
       automatic 16-color PS2 palette quantization and auto-sync across identical
       language slots).
     - Rebuilds all levels of nested .FHM containers while preserving 16-byte
       alignment and the original header layout.
  4. Direct Batch Export of all GIM textures and Dynamic Font Atlases from .FHM
     into PNG_OUT (export_hdtex mode):
     - GIM textures are saved as <fhm>_<group>_<HASH>.png.
     - Dynamic fonts (*Pmenu.fhm, pause_data_*.fhm, etc.) are exported as
       <fhm>_<idx>_font_<HASH>.png + .json (ready for mods/textures/).

Usage:
  Interactive menu:
    python ACZ_FHM_Converter.py

  Command line:
    python ACZ_FHM_Converter.py unpack [file_or_folder_path] [output_folder]
    python ACZ_FHM_Converter.py repack [unpacked_folder_path] [output_folder]
    python ACZ_FHM_Converter.py export_hdtex [fhm_folder_path] [png_out_folder]

Version History:
  - v1: Initial .FHM container unpacker and repacker with 16-byte sector alignment.
  - v2: Added recursive nested .FHM hierarchy unpacking (group_XX/) with manifest.json
        and automatic embedded GIM <-> PNG conversion.
  - v3: Added bitmap font UV coordinate & character width metric table decoding/encoding
        (*_font_metric.json for debrief/opm_fontdata.fhm and hud/huddataN.fhm).
  - v4: Added dynamic tile font (GameFont / CUnicodeFont / *Pmenu.fhm at 0x1401FFBD0)
        decoding/encoding (*_pmenu_font.json + RGBA glyph atlas PNG) across all 7
        language slots with automatic cross-slot sync on repack.
  - v5: Added 16x16 (log2=4) and 64x64 (log2=6) dynamic font support, triple 64-bit
        FNV-1a font hashing (hdtex_hash, hdtex_hash_clean, hdtex_hash_visual), GIM
        FNV-1a hashing, and Mode 3 (export_hdtex) batch export for acz_hdtex.dll.
"""

import json
import os
import struct
import sys
from typing import List, Optional, Tuple

import numpy as np
from PIL import Image

CONVERTER_VERSION = "v5"

# Default directories
FHM_IN_DIR = "FHM_IN"
FHM_UNPACKED_DIR = "FHM_UNPACKED"
FHM_OUT_DIR = "FHM_OUT"
PNG_OUT_DIR = "PNG_OUT"

GIM_MAGIC = b"GIM\x00"

# Language slot mapping in 7-language containers (switch at 0x1400EC560 in acz.exe)
PMENU_LANG_TAGS = {
    0: "JP_NTSC",
    1: "EN_US",
    2: "EN_UK",
    3: "IT",
    4: "DE",
    5: "FR",
    6: "ES",
}


# =============================================================================
# 1. HELPER FUNCTIONS FOR GIM (4-BIT PSMT4 / 8-BIT PSMT8) & FNV-1A HASHING
# =============================================================================

def calc_fnv1a64(data: bytes) -> int:
    """
    Computes the 64-bit FNV-1a hash for a GIM\\0 buffer (matching acz_hdtex.dll).
    Byte 0x12 (tbw) is zeroed and PSM 0x1B at 0x13 is normalized to 0x13.
    """
    h = 0xCBF29CE484222325
    prime = 0x100000001B3
    for i, b in enumerate(data):
        if i == 0x12:
            b = 0
        elif i == 0x13 and b == 0x1B:
            b = 0x13
        h ^= b
        h = (h * prime) & 0xFFFFFFFFFFFFFFFF
    return h


def calc_dynamic_font_hashes(data: bytes) -> Tuple[int, int, int]:
    """
    Computes three 64-bit FNV-1a hashes for a dynamic tile font (GameFont / CUnicodeFont / Pmenu),
    matching SafeInspectAndHashGameFont in acz_hdtex.dll:
      - hash_norm:   runtime_tbp (+4..+7) and flags (+10) of each glyph zeroed
      - hash_clean:  uninitialized 0xCD padding bytes also zeroed
      - hash_visual: visual atlas hash over (num_glyphs, glyph_dim, palette, tiles)
    """
    num_strs, num_glyphs, unk06, glyph_dim, cbp, tbp, max_adv = struct.unpack_from("<IHHHHHH", data, 0)
    rdi_off = 16 * (num_glyphs + 1)
    r10_off = ((rdi_off + 2 * num_strs) & ~3) + 4
    last_len = struct.unpack_from("<h", data, rdi_off + 2 * (num_strs - 1))[0]
    last_off = struct.unpack_from("<i", data, r10_off + 4 * (num_strs - 1))[0]
    end_idx_off = r10_off + 2 * (last_off + 2 * num_strs + last_len)
    pal_off = (end_idx_off & ~15) + 16
    bmp_off = pal_off + 0x40
    bmp_size = (num_glyphs * glyph_dim * glyph_dim) // 2
    total_size = bmp_off + bmp_size

    b_norm = bytearray(data[:total_size])
    b_clean = bytearray(data[:total_size])
    for g in range(num_glyphs):
        off = 16 + g * 16
        b_norm[off + 4 : off + 8] = b"\x00\x00\x00\x00"
        b_norm[off + 10] = 0
        b_clean[off + 4 : off + 8] = b"\x00\x00\x00\x00"
        b_clean[off + 10 : off + 16] = b"\x00\x00\x00\x00\x00\x00"

    b_clean[rdi_off + 2 * num_strs : r10_off] = b"\x00" * (r10_off - (rdi_off + 2 * num_strs))
    b_clean[end_idx_off:pal_off] = b"\x00" * (pal_off - end_idx_off)

    def _fnv1a(buf: bytes) -> int:
        h = 0xCBF29CE484222325
        prime = 0x100000001B3
        for x in buf:
            h ^= x
            h = (h * prime) & 0xFFFFFFFFFFFFFFFF
        return h

    h_norm = _fnv1a(b_norm)
    h_clean = _fnv1a(b_clean)
    vis_buf = struct.pack("<HH", num_glyphs, glyph_dim) + data[pal_off:total_size]
    h_vis = _fnv1a(vis_buf)
    return h_norm, h_clean, h_vis


def csm1_swizzle_palette(palette: np.ndarray) -> np.ndarray:
    """PS2 GS CSM1 palette block permutation (swaps +8..+15 and +16..+23 in every 32-color block)."""
    out = palette.copy()
    num_colors = len(out)
    for base in range(0, num_colors, 32):
        if base + 24 <= num_colors:
            out[base + 8 : base + 16] = palette[base + 16 : base + 24]
            out[base + 16 : base + 24] = palette[base + 8 : base + 16]
    return out


def ps2_alpha_to_pc(a_ps2: np.ndarray) -> np.ndarray:
    """Scales PS2 alpha channel (0..128 -> 0..255)."""
    v = (a_ps2.astype(np.uint32) * 255 + 64) // 128
    return np.clip(v, 0, 255).astype(np.uint8)


def pc_alpha_to_ps2(a_pc: np.ndarray) -> np.ndarray:
    """Scales PC alpha channel back to PS2 GS range (0..255 -> 0..128)."""
    return np.clip(np.round(a_pc.astype(np.float32) * (128.0 / 255.0)), 0, 128).astype(np.uint8)


def decode_gim_bytes(gim_data: bytes) -> Tuple[Image.Image, dict]:
    """
    Decodes a GIM\\0 texture buffer (4-bit PSMT4 or 8-bit PSMT8) into an RGBA PIL.Image.
    Returns (PIL.Image, header_metadata_dict).
    """
    if len(gim_data) < 0x30 or gim_data[:4] != GIM_MAGIC:
        raise ValueError("Buffer is not a valid GIM\\0 file.")

    flags = struct.unpack_from("<I", gim_data, 0x04)[0]
    tbp0 = struct.unpack_from("<H", gim_data, 0x10)[0]
    tbw = gim_data[0x12]
    psm = gim_data[0x13]
    width, height = struct.unpack_from("<HH", gim_data, 0x1C)

    if psm in (0x13, 0x1B):  # PSMT8 / PSMT8H (8-bit, 256 colors)
        bpp = 8
        raw_pix_len = width * height
    elif psm in (0x14, 0x24):  # PSMT4 (4-bit, 16 colors)
        bpp = 4
        raw_pix_len = (width * height + 1) // 2
    else:
        raise ValueError(f"Unsupported GIM pixel format: psm=0x{psm:02X} ({width}x{height})")

    aligned_pix_len = (raw_pix_len + 15) & ~15
    clut_off = 0x20 + aligned_pix_len
    if len(gim_data) < clut_off + 0x10:
        raise ValueError("Truncated GIM buffer (missing CLUT header).")

    clut_hdr = gim_data[clut_off : clut_off + 0x10]
    cbp = struct.unpack_from("<H", clut_hdr, 0x00)[0]
    clut_psm = clut_hdr[0x03]
    clut_w, clut_h = struct.unpack_from("<HH", clut_hdr, 0x0C)
    num_colors = clut_w * clut_h
    pal_off = clut_off + 0x10
    pal_bytes = gim_data[pal_off : pal_off + num_colors * 4]

    pal = np.frombuffer(pal_bytes, dtype=np.uint8).reshape(-1, 4).copy()
    pal[:, 3] = ps2_alpha_to_pc(pal[:, 3])

    if bpp == 8 and num_colors >= 32:
        pal = csm1_swizzle_palette(pal)

    pix_buf = np.frombuffer(gim_data[0x20 : 0x20 + raw_pix_len], dtype=np.uint8)
    if bpp == 8:
        indices = pix_buf.reshape((height, width))
    else:
        low = pix_buf & 0x0F
        high = (pix_buf >> 4) & 0x0F
        unpacked = np.empty(raw_pix_len * 2, dtype=np.uint8)
        unpacked[0::2] = low
        unpacked[1::2] = high
        indices = unpacked[: width * height].reshape((height, width))

    rgba = pal[indices]
    img = Image.fromarray(rgba, mode="RGBA")
    exact_size = pal_off + num_colors * 4
    hash_val = calc_fnv1a64(gim_data[:exact_size])
    meta = {
        "width": width,
        "height": height,
        "psm": psm,
        "bpp": bpp,
        "flags": flags,
        "tbp0": tbp0,
        "tbw": tbw,
        "cbp": cbp,
        "clut_psm": clut_psm,
        "clut_w": clut_w,
        "clut_h": clut_h,
        "header_0_20_hex": gim_data[:0x20].hex(),
        "clut_hdr_hex": clut_hdr.hex(),
        "hdtex_hash": f"{hash_val:016X}",
    }
    return img, meta


def quantize_rgba_to_ps2_palette(flat_pixels: np.ndarray, max_colors: int) -> Tuple[np.ndarray, np.ndarray]:
    """Quantizes an array of RGBA pixels (N, 4) into a PS2 palette (max_colors, 4) and indices (N,)."""
    unique_cols, inverse_idx = np.unique(flat_pixels, axis=0, return_inverse=True)
    if len(unique_cols) <= max_colors:
        pal_rgba = np.zeros((max_colors, 4), dtype=np.uint8)
        pal_rgba[: len(unique_cols)] = unique_cols
        indices = inverse_idx.astype(np.uint8)
    else:
        _, counts = np.unique(flat_pixels, axis=0, return_counts=True)
        order = np.argsort(-counts)
        seeds = unique_cols[order[:max_colors]].astype(np.float32)
        pts = unique_cols.astype(np.float32)
        for _ in range(8):
            dists = np.sum((pts[:, None, :] - seeds[None, :, :]) ** 2, axis=2)
            labels = np.argmin(dists, axis=1)
            for k in range(max_colors):
                mask = labels == k
                if np.any(mask):
                    seeds[k] = np.average(pts[mask], axis=0, weights=counts[mask])
        pal_rgba = np.clip(np.round(seeds), 0, 255).astype(np.uint8)
        dists = np.sum((pts[:, None, :] - pal_rgba[None, :, :].astype(np.float32)) ** 2, axis=2)
        map_u = np.argmin(dists, axis=1).astype(np.uint8)
        indices = map_u[inverse_idx]
    return pal_rgba, indices


def encode_gim_bytes(img: Image.Image, orig_gim_data: Optional[bytes] = None, meta: Optional[dict] = None) -> bytes:
    """
    Encodes a PIL.Image back into binary GIM\\0 format, preserving the original header
    and palette bit depth (4-bit PSMT4 or 8-bit PSMT8).
    """
    img_rgba = img.convert("RGBA")
    width, height = img_rgba.size

    if orig_gim_data is not None and len(orig_gim_data) >= 0x30 and orig_gim_data[:4] == GIM_MAGIC:
        hdr_0_20 = bytearray(orig_gim_data[:0x20])
        psm = hdr_0_20[0x13]
        orig_w, orig_h = struct.unpack_from("<HH", hdr_0_20, 0x1C)
        orig_pix_len = ((orig_w * orig_h + 1) // 2) if psm in (0x14, 0x24) else (orig_w * orig_h)
        orig_clut_off = 0x20 + ((orig_pix_len + 15) & ~15)
        clut_hdr = bytearray(orig_gim_data[orig_clut_off : orig_clut_off + 0x10])
    elif meta is not None:
        hdr_0_20 = bytearray.fromhex(meta["header_0_20_hex"])
        clut_hdr = bytearray.fromhex(meta["clut_hdr_hex"])
        psm = hdr_0_20[0x13]
    else:
        psm = 0x14
        hdr_0_20 = bytearray(0x20)
        hdr_0_20[:4] = GIM_MAGIC
        struct.pack_into("<I", hdr_0_20, 0x04, 1)
        hdr_0_20[0x13] = psm
        clut_hdr = bytearray(0x10)
        struct.pack_into("<HH", clut_hdr, 0x0C, 8, 2)

    struct.pack_into("<HH", hdr_0_20, 0x1C, width, height)
    tbw = max(1, (width + 63) // 64)
    if psm in (0x13, 0x14) and (tbw & 1):
        tbw += 1
    hdr_0_20[0x12] = tbw

    max_colors = 16 if psm in (0x14, 0x24) else 256
    arr = np.array(img_rgba, dtype=np.uint8)
    pal_rgba, indices = quantize_rgba_to_ps2_palette(arr.reshape(-1, 4), max_colors)

    pal_rgba[:, 3] = pc_alpha_to_ps2(pal_rgba[:, 3])
    if psm in (0x13, 0x1B):
        pal_rgba = csm1_swizzle_palette(pal_rgba)

    if psm in (0x13, 0x1B):
        pix_bytes = indices.tobytes()
    else:
        if len(indices) % 2 != 0:
            indices = np.append(indices, np.uint8(0))
        low = indices[0::2] & 0x0F
        high = (indices[1::2] & 0x0F) << 4
        packed = (low | high).astype(np.uint8)
        pix_bytes = packed.tobytes()

    pad_len = ((len(pix_bytes) + 15) & ~15) - len(pix_bytes)
    pix_padded = pix_bytes + (b"\x00" * pad_len)

    return bytes(hdr_0_20) + pix_padded + bytes(clut_hdr) + pal_rgba.tobytes()


# =============================================================================
# 2. DYNAMIC FONT DECODER / ENCODER (GameFont / CUnicodeFont / Pmenu)
#    Exact implementation of parser 0x1401FFBD0 in acz.exe
# =============================================================================

def is_pmenu_font_entry(data: bytes) -> bool:
    """
    Checks whether a binary block is a dynamic tile font entry
    (GameFont / CSwgGameFont / CUnicodeFont / *Pmenu.fhm / pause_data_*.fhm).
    Supports both 32x32 (unk06 == 5) and 16x16 (unk06 == 4) fonts.
    """
    if len(data) < 0x60 or data[:4] == GIM_MAGIC:
        return False
    num_strs, num_glyphs, unk06, glyph_dim, cbp, tbp, max_adv = struct.unpack_from("<IHHHHHH", data, 0)
    if not (1 <= num_strs <= 4096 and 1 <= num_glyphs <= 4096 and glyph_dim in (12, 16, 20, 24, 32, 48, 64)):
        return False
    if (1 << unk06) != glyph_dim and unk06 not in (4, 5, 6):
        return False
    rdi_off = 16 * (num_glyphs + 1)
    r10_off = ((rdi_off + 2 * num_strs) & ~3) + 4
    if r10_off + 4 * num_strs > len(data):
        return False
    last_len = struct.unpack_from("<h", data, rdi_off + 2 * (num_strs - 1))[0]
    last_off = struct.unpack_from("<i", data, r10_off + 4 * (num_strs - 1))[0]
    if last_len < 0 or last_off < 0:
        return False
    end_idx_off = r10_off + 2 * (last_off + 2 * num_strs + last_len)
    pal_off = (end_idx_off & ~15) + 16
    bmp_off = pal_off + 0x40
    bmp_size = (num_glyphs * glyph_dim * glyph_dim) // 2
    return bmp_off + bmp_size <= len(data) <= bmp_off + bmp_size + 32


def decode_pmenu_font_entry(data: bytes) -> Tuple[Image.Image, dict]:
    """
    Decodes a dynamic font entry into:
      - An RGBA glyph atlas image (8 glyphs per row)
      - A JSON dictionary containing the 64-bit hash for acz_hdtex.dll,
        decoded UTF-16LE strings, per-glyph metrics, and PS2 palette.
    """
    num_strs, num_glyphs, unk06, glyph_dim, cbp, tbp, max_adv = struct.unpack_from("<IHHHHHH", data, 0)
    h_norm, h_clean, h_vis = calc_dynamic_font_hashes(data)

    glyphs_meta = []
    for g in range(num_glyphs):
        off = 16 + g * 16
        adv_x, height, runtime_tbp, char_code, flags = struct.unpack_from("<HHIHB", data, off)
        tail_hex = data[off + 11 : off + 16].hex()
        ch = chr(char_code) if char_code != 0 else "\x00"
        glyphs_meta.append({
            "index": g,
            "char": ch,
            "codepoint_hex": f"0x{char_code:04X}",
            "advance_x": adv_x,
            "height": height,
            "runtime_tbp": runtime_tbp,
            "flags": flags,
            "reserved_hex": tail_hex,
        })

    rdi_off = 16 * (num_glyphs + 1)
    r10_off = ((rdi_off + 2 * num_strs) & ~3) + 4
    rax_off = r10_off + 4 * num_strs

    str_lengths = [struct.unpack_from("<h", data, rdi_off + 2 * s)[0] for s in range(num_strs)]
    str_offsets = [struct.unpack_from("<i", data, r10_off + 4 * s)[0] for s in range(num_strs)]

    strings_decoded = []
    for s in range(num_strs):
        slen = str_lengths[s]
        soff = str_offsets[s]
        idxs = [struct.unpack_from("<h", data, rax_off + 2 * (soff + k))[0] for k in range(slen)]
        chars = []
        for idx in idxs:
            if idx < 0:
                chars.append("\n")
            elif idx < len(glyphs_meta):
                chars.append(glyphs_meta[idx]["char"])
            else:
                chars.append(f"[{idx}]")
        strings_decoded.append({
            "string_index": s,
            "text": "".join(chars),
            "glyph_indices": idxs,
        })

    end_idx_off = r10_off + 2 * (str_offsets[-1] + 2 * num_strs + str_lengths[-1])
    pal_off = (end_idx_off & ~15) + 16
    bmp_off = pal_off + 0x40

    pal_raw = np.frombuffer(data[pal_off : pal_off + 64], dtype=np.uint8).reshape(16, 4).copy()
    pal_pc = pal_raw.copy()
    pal_pc[:, 3] = ps2_alpha_to_pc(pal_pc[:, 3])

    cols = 8
    rows = (num_glyphs + cols - 1) // cols
    img_arr = np.zeros((rows * glyph_dim, cols * glyph_dim, 4), dtype=np.uint8)

    tile_bytes = (glyph_dim * glyph_dim) // 2
    for g in range(num_glyphs):
        g_off = bmp_off + g * tile_bytes
        raw_tile = np.frombuffer(data[g_off : g_off + tile_bytes], dtype=np.uint8)
        low = raw_tile & 0x0F
        high = (raw_tile >> 4) & 0x0F
        unpacked = np.empty(tile_bytes * 2, dtype=np.uint8)
        unpacked[0::2] = low
        unpacked[1::2] = high
        tile_indices = unpacked.reshape((glyph_dim, glyph_dim))
        gx0 = (g % cols) * glyph_dim
        gy0 = (g // cols) * glyph_dim
        img_arr[gy0 : gy0 + glyph_dim, gx0 : gx0 + glyph_dim] = pal_pc[tile_indices]

    img = Image.fromarray(img_arr, mode="RGBA")
    info = {
        "format": "ACZ_PMENU_FONT",
        "hdtex_hash": f"{h_norm:016X}",
        "hdtex_hash_clean": f"{h_clean:016X}",
        "hdtex_hash_visual": f"{h_vis:016X}",
        "num_strings": num_strs,
        "num_glyphs": num_glyphs,
        "unk06": unk06,
        "glyph_dim": glyph_dim,
        "vram_clut_cbp": f"0x{cbp:04X}",
        "vram_tex_tbp": f"0x{tbp:04X}",
        "max_advance": max_adv,
        "grid_columns": cols,
        "palette_ps2_rgba": pal_raw.tolist(),
        "strings": strings_decoded,
        "glyphs": glyphs_meta,
        "padding_r10_hex": data[rdi_off + 2 * num_strs : r10_off].hex(),
        "padding_pal_hex": data[end_idx_off:pal_off].hex(),
    }
    return img, info


def encode_pmenu_font_entry(img: Image.Image, info: dict) -> bytes:
    """
    Rebuilds a binary dynamic font entry from an edited *_glyphs*.png and *_pmenu_font.json
    preserving exact 0x1401FFBD0 alignment. If new colors were introduced in the PNG,
    automatically re-quantizes the 16-color PS2 palette.
    """
    glyphs_meta = info["glyphs"]
    strings_info = info["strings"]
    num_glyphs = len(glyphs_meta)
    num_strs = len(strings_info)
    unk06 = int(info.get("unk06", 5))
    glyph_dim = int(info.get("glyph_dim", 32))
    cbp = int(info.get("vram_clut_cbp", "0xE000"), 16)
    tbp = int(info.get("vram_tex_tbp", "0xE001"), 16)
    max_adv = int(info.get("max_advance", 14))
    cols = int(info.get("grid_columns", 8))

    out = bytearray()
    out += struct.pack("<IHHHHHH", num_strs, num_glyphs, unk06, glyph_dim, cbp, tbp, max_adv)

    for g_meta in glyphs_meta:
        char_str = g_meta.get("char", "")
        if len(char_str) == 1:
            ccode = ord(char_str)
        else:
            ccode = int(g_meta["codepoint_hex"], 16)
        adv_x = int(g_meta["advance_x"])
        height = int(g_meta["height"])
        rtbp = int(g_meta.get("runtime_tbp", 0))
        flags = int(g_meta.get("flags", 0))
        res_bytes = bytes.fromhex(g_meta.get("reserved_hex", "cdcdcdcdcd"))[:5].ljust(5, b"\xcd")
        out += struct.pack("<HHIHB", adv_x, height, rtbp, ccode, flags) + res_bytes

    rdi_off = len(out)
    r10_off = ((rdi_off + 2 * num_strs) & ~3) + 4

    all_indices: List[int] = []
    str_lengths: List[int] = []
    str_offsets: List[int] = []
    for s_obj in strings_info:
        idxs = [int(x) for x in s_obj["glyph_indices"]]
        str_offsets.append(len(all_indices))
        str_lengths.append(len(idxs))
        all_indices.extend(idxs)

    for slen in str_lengths:
        out += struct.pack("<h", slen)

    pad_r10_len = r10_off - len(out)
    orig_pad_r10 = bytes.fromhex(info.get("padding_r10_hex", ""))
    if len(orig_pad_r10) == pad_r10_len:
        out += orig_pad_r10
    else:
        out += b"\x00" * pad_r10_len

    for soff in str_offsets:
        out += struct.pack("<i", soff)

    for idx in all_indices:
        out += struct.pack("<h", idx)

    end_idx_off = len(out)
    pal_off = (end_idx_off & ~15) + 16
    pad_pal_len = pal_off - len(out)
    orig_pad_pal = bytes.fromhex(info.get("padding_pal_hex", ""))
    if len(orig_pad_pal) == pad_pal_len:
        out += orig_pad_pal
    else:
        out += b"\x00" * pad_pal_len

    rows = (num_glyphs + cols - 1) // cols
    expected_w = cols * glyph_dim
    expected_h = rows * glyph_dim
    img_rgba_pil = img.convert("RGBA")
    if img_rgba_pil.size != (expected_w, expected_h):
        img_rgba_pil = img_rgba_pil.resize((expected_w, expected_h), Image.Resampling.LANCZOS)

    img_u8 = np.array(img_rgba_pil, dtype=np.uint8)
    pal_ps2_orig = np.array(info["palette_ps2_rgba"], dtype=np.uint8).reshape(16, 4)
    pal_pc_orig = pal_ps2_orig.copy()
    pal_pc_orig[:, 3] = ps2_alpha_to_pc(pal_ps2_orig[:, 3])

    flat_u8 = img_u8.reshape(-1, 4)
    dists_orig = np.sum((flat_u8[:, None, :].astype(np.float32) - pal_pc_orig[None, :, :].astype(np.float32)) ** 2, axis=2)
    max_color_err = float(np.max(np.min(dists_orig, axis=1)))
    if max_color_err > 120.0:
        pal_pc_new, _ = quantize_rgba_to_ps2_palette(flat_u8, 16)
        pal_ps2 = pal_pc_new.copy()
        pal_ps2[:, 3] = pc_alpha_to_ps2(pal_pc_new[:, 3])
        pal_pc = pal_pc_new.astype(np.float32)
    else:
        pal_ps2 = pal_ps2_orig
        pal_pc = pal_pc_orig.astype(np.float32)

    out += pal_ps2.tobytes()

    img_rgba = img_u8.astype(np.float32)
    for g in range(num_glyphs):
        gx0 = (g % cols) * glyph_dim
        gy0 = (g // cols) * glyph_dim
        tile = img_rgba[gy0 : gy0 + glyph_dim, gx0 : gx0 + glyph_dim].reshape(-1, 4)
        dists = np.sum((tile[:, None, :] - pal_pc[None, :, :]) ** 2, axis=2)
        idx_tile = np.argmin(dists, axis=1).astype(np.uint8)
        low = idx_tile[0::2] & 0x0F
        high = (idx_tile[1::2] & 0x0F) << 4
        out += (low | high).astype(np.uint8).tobytes()

    return bytes(out)


# =============================================================================
# 3. BITMAP FONT METRIC TABLE DECODER (opm_fontdata.fhm / huddataN.fhm)
# =============================================================================

def try_decode_font_metric_table(data: bytes) -> Optional[dict]:
    """
    Checks and decodes a bitmap font UV coordinate and character width table
    (entries [0][1..7] in opm_fontdata.fhm and huddataN.fhm).
    """
    if len(data) < 16 or (len(data) % 16) != 0:
        return None
    line_height = data[0]
    glyph_count = data[1]
    if not (5 <= line_height <= 32 and 8 <= glyph_count <= 128):
        return None
    expected_raw_len = 8 + glyph_count * 4
    expected_aligned_len = (expected_raw_len + 15) & ~15
    if expected_aligned_len != len(data):
        return None
    if any(b != 0 for b in data[expected_raw_len:]):
        return None

    glyphs = []
    for i in range(glyph_count):
        c_code, u, v, w = struct.unpack_from("<4B", data, 8 + i * 4)
        if w == 0 or w > 32:
            return None
        ch = chr(c_code) if 32 <= c_code <= 126 or c_code >= 160 else f"0x{c_code:02X}"
        glyphs.append({
            "char": ch,
            "code_hex": f"0x{c_code:02X}",
            "u": u,
            "v": v,
            "width": w,
            "height": line_height,
        })

    return {
        "format": "ACZ_BITMAP_FONT_METRIC_TABLE",
        "line_height": line_height,
        "glyph_count": glyph_count,
        "header_params_hex": data[2:8].hex(),
        "glyphs": glyphs,
    }


def encode_font_metric_table(info: dict) -> bytes:
    """Rebuilds a bitmap font UV metric table from JSON."""
    line_height = int(info["line_height"])
    glyphs = info["glyphs"]
    glyph_count = len(glyphs)
    hdr_params = bytes.fromhex(info["header_params_hex"])[:6].ljust(6, b"\x00")
    out = bytearray([line_height, glyph_count]) + hdr_params
    for g in glyphs:
        c_code = int(g["code_hex"], 16)
        out += struct.pack("<4B", c_code, int(g["u"]), int(g["v"]), int(g["width"]))
    pad_len = ((len(out) + 15) & ~15) - len(out)
    out += b"\x00" * pad_len
    return bytes(out)


# =============================================================================
# 4. .FHM CONTAINER PARSER & BUILDER (WITH NESTED HIERARCHY SUPPORT)
# =============================================================================

def parse_fhm_header(data: bytes) -> Optional[Tuple[int, List[int], int, int]]:
    """
    Checks whether `data` is an .FHM container.
    Returns (count, offsets, end_marker, header_size) or None.
    """
    if len(data) < 16 or data[:4] == GIM_MAGIC:
        return None
    count = struct.unpack_from("<I", data, 0)[0]
    if count == 0 or count > 512:
        return None
    if 4 * (count + 1) > len(data):
        return None

    offs = [struct.unpack_from("<I", data, 4 + 4 * i)[0] for i in range(count)]
    table_bytes = 4 * (count + 1)
    min_first = (table_bytes + 15) & ~15
    if offs[0] < table_bytes or offs[0] > min_first + 32 or (offs[0] & 0xF) != 0:
        return None

    if table_bytes + 4 <= offs[0]:
        end_marker = struct.unpack_from("<I", data, table_bytes)[0]
        if end_marker not in (0, len(data)):
            return None
        pad_start = 4 * (count + 2) if end_marker == len(data) else table_bytes
        if pad_start <= offs[0] and any(b != 0 for b in data[pad_start : offs[0]]):
            return None
    else:
        end_marker = 0

    for i in range(count):
        if (offs[i] & 0xF) != 0 or offs[i] >= len(data):
            return None
        if i + 1 < count and offs[i] >= offs[i + 1]:
            return None

    if is_pmenu_font_entry(data):
        return None

    return count, offs, end_marker, offs[0]


def unpack_fhm_node(data: bytes, out_dir: str, rel_prefix: str = "") -> dict:
    """
    Recursively unpacks an .FHM node (container or leaf entry) into `out_dir`.
    Returns node metadata for manifest.json.
    """
    os.makedirs(out_dir, exist_ok=True)
    fhm_hdr = parse_fhm_header(data)

    if fhm_hdr is not None:
        count, offs, end_marker, header_size = fhm_hdr
        children = []
        for i in range(count):
            start = offs[i]
            end = offs[i + 1] if i + 1 < count else len(data)
            sub_data = data[start:end]
            sub_fhm = parse_fhm_header(sub_data)
            if sub_fhm is not None:
                sub_dir_name = f"group_{i:02d}"
                sub_dir_path = os.path.join(out_dir, sub_dir_name)
                child_prefix = f"{rel_prefix}{sub_dir_name}/"
                child_meta = unpack_fhm_node(sub_data, sub_dir_path, child_prefix)
                child_meta["index"] = i
                child_meta["dir"] = sub_dir_name
                children.append(child_meta)
            else:
                leaf_meta = save_leaf_entry(sub_data, out_dir, i, rel_prefix, parent_count=count)
                children.append(leaf_meta)

        return {
            "node_type": "fhm_container",
            "count": count,
            "has_size_end_marker": (end_marker == len(data) and len(data) > 0),
            "header_size": header_size,
            "children": children,
        }
    else:
        return save_leaf_entry(data, out_dir, 0, rel_prefix, parent_count=1)


def save_leaf_entry(data: bytes, out_dir: str, index: int, rel_prefix: str = "", parent_count: int = 1) -> dict:
    """Saves a leaf .FHM entry (GIM texture, Pmenu dynamic font, metric table, or raw binary block)."""
    base_name = f"entry_{index:02d}"

    # Case 1: Embedded GIM\0 texture
    if len(data) >= 0x30 and data[:4] == GIM_MAGIC:
        gim_filename = f"{base_name}.gim"
        gim_path = os.path.join(out_dir, gim_filename)
        with open(gim_path, "wb") as f:
            f.write(data)

        try:
            img, gim_meta = decode_gim_bytes(data)
            h_hex = gim_meta["hdtex_hash"]
            png_filename = f"{base_name}_{h_hex}.png"
            png_path = os.path.join(out_dir, png_filename)
            img.save(png_path, "PNG")
            print(
                f"  [GIM->PNG] {rel_prefix}{png_filename} "
                f"({gim_meta['width']}x{gim_meta['height']}, {gim_meta['bpp']}bpp, "
                f"HDTex Hash: {h_hex})"
            )
            return {
                "index": index,
                "node_type": "gim_texture",
                "gim_file": gim_filename,
                "png_file": png_filename,
                "gim_meta": gim_meta,
            }
        except Exception as e:
            print(f"  [WARN] Failed to decode {rel_prefix}{gim_filename} to PNG: {e}")
            return {
                "index": index,
                "node_type": "raw_binary",
                "raw_file": gim_filename,
            }

    # Case 2: Dynamic tile font and text block (GameFont / CUnicodeFont / Pmenu)
    if is_pmenu_font_entry(data):
        img, pmenu_info = decode_pmenu_font_entry(data)
        h_hex = pmenu_info["hdtex_hash"]
        lang_tag = PMENU_LANG_TAGS.get(index, f"SLOT_{index:02d}") if parent_count == 7 else f"SLOT_{index:02d}"
        pmenu_info["language_slot"] = lang_tag

        raw_filename = f"{base_name}.bin"
        png_filename = f"{base_name}_glyphs_{h_hex}.png"
        legacy_png_filename = f"{base_name}_glyphs.png"
        json_filename = f"{base_name}_pmenu_font.json"

        with open(os.path.join(out_dir, raw_filename), "wb") as f:
            f.write(data)

        img.save(os.path.join(out_dir, png_filename), "PNG")
        with open(os.path.join(out_dir, json_filename), "w", encoding="utf-8") as jf:
            json.dump(pmenu_info, jf, ensure_ascii=False, indent=2)

        preview_text = pmenu_info["strings"][0]["text"].replace("\n", " / ") if pmenu_info["strings"] else ""
        if len(preview_text) > 50:
            preview_text = preview_text[:47] + "..."
        print(
            f"  [DYNAMIC FONT {lang_tag}] {rel_prefix}{png_filename} "
            f"({pmenu_info['num_glyphs']} glyphs {pmenu_info['glyph_dim']}x{pmenu_info['glyph_dim']}, "
            f'HDTex Hash: {h_hex}) | Text: "{preview_text}"'
        )
        return {
            "index": index,
            "node_type": "pmenu_font",
            "language_slot": lang_tag,
            "hdtex_hash": h_hex,
            "raw_file": raw_filename,
            "png_file": png_filename,
            "legacy_png_file": legacy_png_filename,
            "json_file": json_filename,
        }

    # Case 3: Bitmap font UV metric table (opm_fontdata / huddataN)
    metric_info = try_decode_font_metric_table(data)
    if metric_info is not None:
        raw_filename = f"{base_name}.bin"
        json_filename = f"{base_name}_font_metric.json"
        with open(os.path.join(out_dir, raw_filename), "wb") as f:
            f.write(data)
        with open(os.path.join(out_dir, json_filename), "w", encoding="utf-8") as jf:
            json.dump(metric_info, jf, ensure_ascii=False, indent=2)
        print(
            f"  [FONT METRIC] {rel_prefix}{json_filename} "
            f"(height={metric_info['line_height']}px, glyphs={metric_info['glyph_count']})"
        )
        return {
            "index": index,
            "node_type": "font_metric_table",
            "raw_file": raw_filename,
            "json_file": json_filename,
        }

    # Case 4: Other binary data / tables
    raw_filename = f"{base_name}.bin"
    with open(os.path.join(out_dir, raw_filename), "wb") as f:
        f.write(data)
    return {
        "index": index,
        "node_type": "raw_binary",
        "raw_file": raw_filename,
    }


def _resolve_pmenu_png_path(in_dir: str, node_meta: dict) -> Optional[str]:
    """Finds the active glyph PNG file (either with hash suffix or legacy entry_XX_glyphs.png)."""
    candidates = [
        node_meta.get("png_file"),
        node_meta.get("legacy_png_file"),
        f"entry_{int(node_meta.get('index', 0)):02d}_glyphs.png",
    ]
    existing = [os.path.join(in_dir, c) for c in candidates if c and os.path.exists(os.path.join(in_dir, c))]
    if not existing:
        return None
    existing.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return existing[0]


def pack_fhm_node(node_meta: dict, in_dir: str) -> bytes:
    """
    Recursively rebuilds an .FHM binary buffer from an unpacked folder and manifest.json.
    If in a 7-language container (e.g. pause_data_ntsc.fhm) the user edited entry_00_glyphs*.png
    while entry_01 (EN_US) and entry_02 (EN_UK) share the exact same glyph set and were not
    edited separately, automatically syncs the edited atlas to those slots as well.
    """
    ntype = node_meta["node_type"]

    if ntype == "fhm_container":
        children = node_meta["children"]
        count = len(children)
        has_end_marker = bool(node_meta.get("has_size_end_marker", False))
        min_hdr_size = (4 * (count + (2 if has_end_marker else 1)) + 15) & ~15
        header_size = max(min_hdr_size, int(node_meta.get("header_size", min_hdr_size)))

        edited_pmenu_by_chars = {}
        for ch in children:
            if ch.get("node_type") == "pmenu_font":
                raw_p = os.path.join(in_dir, ch["raw_file"])
                png_p = _resolve_pmenu_png_path(in_dir, ch)
                json_p = os.path.join(in_dir, ch["json_file"])
                if os.path.exists(raw_p) and png_p and os.path.exists(json_p):
                    orig_raw = open(raw_p, "rb").read()
                    orig_img, _ = decode_pmenu_font_entry(orig_raw)
                    cur_img = Image.open(png_p).convert("RGBA")
                    if cur_img.size != orig_img.size or not np.array_equal(np.array(cur_img), np.array(orig_img)):
                        with open(json_p, "r", encoding="utf-8") as jf:
                            j_info = json.load(jf)
                        char_sig = (
                            int(j_info.get("num_glyphs", 0)),
                            int(j_info.get("glyph_dim", 32)),
                            tuple(g.get("codepoint_hex", "") for g in j_info.get("glyphs", [])),
                        )
                        edited_pmenu_by_chars[char_sig] = (cur_img, png_p)

        child_blobs: List[bytes] = []
        for ch in children:
            if ch["node_type"] == "fhm_container":
                sub_dir = os.path.join(in_dir, ch["dir"])
                blob = pack_fhm_node(ch, sub_dir)
            elif ch["node_type"] == "pmenu_font":
                blob = pack_pmenu_leaf(ch, in_dir, edited_pmenu_by_chars)
            else:
                blob = pack_fhm_node(ch, in_dir)
            if len(blob) % 16 != 0:
                blob += b"\x00" * (16 - (len(blob) % 16))
            child_blobs.append(blob)

        offsets = []
        cur_off = header_size
        for blob in child_blobs:
            offsets.append(cur_off)
            cur_off += len(blob)
        total_size = cur_off

        hdr = bytearray()
        hdr += struct.pack("<I", count)
        for off in offsets:
            hdr += struct.pack("<I", off)
        if has_end_marker:
            hdr += struct.pack("<I", total_size)
        hdr += b"\x00" * (header_size - len(hdr))

        return bytes(hdr) + b"".join(child_blobs)

    elif ntype == "gim_texture":
        gim_path = os.path.join(in_dir, node_meta["gim_file"])
        png_path = os.path.join(in_dir, node_meta["png_file"])
        orig_gim = open(gim_path, "rb").read() if os.path.exists(gim_path) else None

        if os.path.exists(png_path):
            img = Image.open(png_path).convert("RGBA")
            if orig_gim is not None:
                orig_img, _ = decode_gim_bytes(orig_gim)
                if img.size == orig_img.size and np.array_equal(np.array(img), np.array(orig_img)):
                    return orig_gim
            return encode_gim_bytes(img, orig_gim, node_meta.get("gim_meta"))
        elif orig_gim is not None:
            return orig_gim
        else:
            raise FileNotFoundError(f"Neither {png_path} nor {gim_path} was found")

    elif ntype == "pmenu_font":
        return pack_pmenu_leaf(node_meta, in_dir, {})

    elif ntype == "font_metric_table":
        raw_path = os.path.join(in_dir, node_meta["raw_file"])
        json_path = os.path.join(in_dir, node_meta["json_file"])
        orig_raw = open(raw_path, "rb").read() if os.path.exists(raw_path) else None

        if os.path.exists(json_path):
            with open(json_path, "r", encoding="utf-8") as jf:
                info = json.load(jf)
            rebuilt = encode_font_metric_table(info)
            if orig_raw is not None and rebuilt == orig_raw:
                return orig_raw
            return rebuilt
        elif orig_raw is not None:
            return orig_raw
        else:
            raise FileNotFoundError(f"Font metric table file not found in {in_dir}")

    else:
        raw_path = os.path.join(in_dir, node_meta["raw_file"])
        with open(raw_path, "rb") as f:
            return f.read()


def pack_pmenu_leaf(node_meta: dict, in_dir: str, edited_pmenu_by_chars: dict) -> bytes:
    """Packs an individual pmenu_font entry with automatic language slot synchronization."""
    raw_path = os.path.join(in_dir, node_meta["raw_file"])
    png_path = _resolve_pmenu_png_path(in_dir, node_meta)
    json_path = os.path.join(in_dir, node_meta["json_file"])
    orig_raw = open(raw_path, "rb").read() if os.path.exists(raw_path) else None

    if png_path and os.path.exists(json_path):
        img = Image.open(png_path).convert("RGBA")
        with open(json_path, "r", encoding="utf-8") as jf:
            info = json.load(jf)

        if orig_raw is not None:
            orig_img, orig_info = decode_pmenu_font_entry(orig_raw)
            same_json = (
                info.get("strings") == orig_info.get("strings")
                and info.get("glyphs") == orig_info.get("glyphs")
                and info.get("palette_ps2_rgba") == orig_info.get("palette_ps2_rgba")
            )
            same_img = (img.size == orig_img.size and np.array_equal(np.array(img), np.array(orig_img)))
            if same_img and same_json:
                char_sig = (
                    int(info.get("num_glyphs", 0)),
                    int(info.get("glyph_dim", 32)),
                    tuple(g.get("codepoint_hex", "") for g in info.get("glyphs", [])),
                )
                if char_sig in edited_pmenu_by_chars:
                    synced_img, src_png = edited_pmenu_by_chars[char_sig]
                    print(
                        f"  [AUTO-SYNC] Applied edited atlas ({os.path.basename(src_png)}) "
                        f"to slot entry_{int(node_meta.get('index', 0)):02d} ({node_meta.get('language_slot', '')})"
                    )
                    return encode_pmenu_font_entry(synced_img, info)
                return orig_raw

        return encode_pmenu_font_entry(img, info)
    elif orig_raw is not None:
        return orig_raw
    else:
        raise FileNotFoundError(f"Pmenu font files not found in {in_dir}")


# =============================================================================
# 5. BATCH EXPORT OF EMBEDDED GIMS & DYNAMIC FONTS TO PNG_OUT (MODE 3)
# =============================================================================

def export_fhm_gims_for_hdtex(
    data: bytes,
    fhm_stem: str,
    out_dir: str,
    prefix: str = "",
    parent_count: int = 1,
    entry_index: int = 0,
) -> Tuple[int, int]:
    """
    Recursively finds and exports:
      1) All GIM\\0 textures inside .FHM into `out_dir` (PNG_OUT) named
         <fhm_stem>_<idx>_<HASH>.png, ready for mods/textures/.
      2) All dynamic tile fonts (GameFont / CUnicodeFont / *Pmenu.fhm / pause_data_*.fhm),
         saving their atlases with 64-bit hashes (<fhm_stem>_<idx>_font_<HASH>.png)
         in `out_dir/pausetxt_pmenu/` for direct replacement via acz_hdtex.dll.
    Returns (exported_gim_count, exported_pmenu_count).
    """
    os.makedirs(out_dir, exist_ok=True)
    fhm_hdr = parse_fhm_header(data)
    exported_gim = 0
    exported_pmenu = 0

    if fhm_hdr is not None:
        count, offs, _, _ = fhm_hdr
        for i in range(count):
            start = offs[i]
            end = offs[i + 1] if i + 1 < count else len(data)
            sub_data = data[start:end]
            sub_prefix = f"{prefix}g{i:02d}_" if prefix == "" else f"{prefix}e{i:02d}_"
            g_cnt, p_cnt = export_fhm_gims_for_hdtex(
                sub_data, fhm_stem, out_dir, sub_prefix, parent_count=count, entry_index=i
            )
            exported_gim += g_cnt
            exported_pmenu += p_cnt
    else:
        if len(data) >= 0x30 and data[:4] == GIM_MAGIC:
            try:
                img, gim_meta = decode_gim_bytes(data)
                h_hex = gim_meta["hdtex_hash"]
                tag = prefix.rstrip("_") if prefix else "00"
                png_name = f"{fhm_stem}_{tag}_{h_hex}.png"
                png_path = os.path.join(out_dir, png_name)
                img.save(png_path, "PNG")
                print(
                    f"  [HDTex GIM] {png_name} "
                    f"({gim_meta['width']}x{gim_meta['height']}, {gim_meta['bpp']}bpp, Hash={h_hex})"
                )
                exported_gim += 1
            except Exception as e:
                print(f"  [WARN] Failed to export GIM from {fhm_stem} ({prefix}): {e}")

        elif is_pmenu_font_entry(data):
            try:
                pmenu_dir = os.path.join(out_dir, "pausetxt_pmenu")
                os.makedirs(pmenu_dir, exist_ok=True)
                img, pmenu_info = decode_pmenu_font_entry(data)
                h_hex = pmenu_info["hdtex_hash"]
                lang_tag = PMENU_LANG_TAGS.get(entry_index, "") if parent_count == 7 else ""
                pmenu_info["language_slot"] = lang_tag
                tag = prefix.rstrip("_") if prefix else "00"
                if lang_tag:
                    tag = f"{tag}_{lang_tag}"
                png_name = f"{fhm_stem}_{tag}_font_{h_hex}.png"
                json_name = f"{fhm_stem}_{tag}_font_{h_hex}.json"
                img.save(os.path.join(pmenu_dir, png_name), "PNG")
                with open(os.path.join(pmenu_dir, json_name), "w", encoding="utf-8") as jf:
                    json.dump(pmenu_info, jf, ensure_ascii=False, indent=2)
                preview_text = pmenu_info["strings"][0]["text"].replace("\n", " / ") if pmenu_info["strings"] else ""
                if len(preview_text) > 45:
                    preview_text = preview_text[:42] + "..."
                print(
                    f"  [HDTex FONT] pausetxt_pmenu/{png_name} "
                    f"({pmenu_info['num_glyphs']} tiles {pmenu_info['glyph_dim']}x{pmenu_info['glyph_dim']}, "
                    f'Hash={h_hex} | "{preview_text}")'
                )
                exported_pmenu += 1
            except Exception as e:
                print(f"  [WARN] Failed to export Pmenu font from {fhm_stem} ({prefix}): {e}")

    return exported_gim, exported_pmenu


def batch_export_hdtex(input_path: str, out_dir: str) -> Tuple[int, int]:
    if os.path.isfile(input_path):
        stem = os.path.splitext(os.path.basename(input_path))[0]
        with open(input_path, "rb") as f:
            return export_fhm_gims_for_hdtex(f.read(), stem, out_dir)

    total_gim = 0
    total_pmenu = 0
    for root, _, files in os.walk(input_path):
        for fn in sorted(files):
            if fn.lower().endswith(".fhm"):
                full_in = os.path.join(root, fn)
                stem = os.path.splitext(fn)[0]
                with open(full_in, "rb") as f:
                    g_cnt, p_cnt = export_fhm_gims_for_hdtex(f.read(), stem, out_dir)
                    total_gim += g_cnt
                    total_pmenu += p_cnt
    return total_gim, total_pmenu


# =============================================================================
# 6. BATCH DIRECTORY PROCESSING & CLI INTERFACE
# =============================================================================

def unpack_fhm_file(fhm_path: str, out_dir: str) -> None:
    with open(fhm_path, "rb") as f:
        data = f.read()
    print(f"=== Unpacking: {fhm_path} ({len(data)} bytes) -> {out_dir} ===")
    manifest = unpack_fhm_node(data, out_dir)
    manifest["source_filename"] = os.path.basename(fhm_path)
    manifest["source_size"] = len(data)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as mf:
        json.dump(manifest, mf, ensure_ascii=False, indent=2)


def repack_fhm_dir(unpacked_dir: str, out_fhm_path: str) -> None:
    manifest_path = os.path.join(unpacked_dir, "manifest.json")
    if not os.path.exists(manifest_path):
        raise FileNotFoundError(f"manifest.json not found in {unpacked_dir}")
    with open(manifest_path, "r", encoding="utf-8") as mf:
        manifest = json.load(mf)
    rebuilt_data = pack_fhm_node(manifest, unpacked_dir)
    os.makedirs(os.path.dirname(os.path.abspath(out_fhm_path)), exist_ok=True)
    with open(out_fhm_path, "wb") as f:
        f.write(rebuilt_data)
    print(f"=== Repacked: {out_fhm_path} ({len(rebuilt_data)} bytes) ===")


def batch_unpack(input_path: str, output_root: str) -> int:
    if os.path.isfile(input_path):
        stem = os.path.splitext(os.path.basename(input_path))[0]
        unpack_fhm_file(input_path, os.path.join(output_root, stem))
        return 1

    count = 0
    for root, _, files in os.walk(input_path):
        for fn in sorted(files):
            if fn.lower().endswith(".fhm"):
                full_in = os.path.join(root, fn)
                rel_dir = os.path.relpath(root, input_path)
                stem = os.path.splitext(fn)[0]
                target_dir = os.path.join(output_root, rel_dir, stem) if rel_dir != "." else os.path.join(output_root, stem)
                unpack_fhm_file(full_in, target_dir)
                count += 1
    return count


def batch_repack(unpacked_root: str, output_root: str) -> int:
    if os.path.exists(os.path.join(unpacked_root, "manifest.json")):
        with open(os.path.join(unpacked_root, "manifest.json"), "r", encoding="utf-8") as mf:
            src_name = json.load(mf).get("source_filename", os.path.basename(unpacked_root.rstrip("/\\")) + ".fhm")
        repack_fhm_dir(unpacked_root, os.path.join(output_root, src_name))
        return 1

    count = 0
    for root, _, files in os.walk(unpacked_root):
        if "manifest.json" in files:
            with open(os.path.join(root, "manifest.json"), "r", encoding="utf-8") as mf:
                manifest = json.load(mf)
            src_name = manifest.get("source_filename", os.path.basename(root) + ".fhm")
            parent_rel = os.path.relpath(os.path.dirname(root), unpacked_root)
            out_fhm = os.path.join(output_root, parent_rel, src_name) if parent_rel != "." else os.path.join(output_root, src_name)
            repack_fhm_dir(root, out_fhm)
            count += 1
    return count


def main() -> None:
    for d in (FHM_IN_DIR, FHM_UNPACKED_DIR, FHM_OUT_DIR, PNG_OUT_DIR):
        os.makedirs(d, exist_ok=True)

    args = sys.argv[1:]
    if not args:
        print("=====================================================================")
        print(f" ACZ .FHM & FONT CONVERTER ({CONVERTER_VERSION} — GIM + Fonts + HDTex Hash) ")
        print("=====================================================================")
        print(f" [1] Unpack all .fhm files from '{FHM_IN_DIR}' -> '{FHM_UNPACKED_DIR}'")
        print(f" [2] Repack .fhm files from '{FHM_UNPACKED_DIR}' -> '{FHM_OUT_DIR}'")
        print(f" [3] Export all GIM textures & font atlases from '{FHM_IN_DIR}' -> '{PNG_OUT_DIR}'")
        print("=====================================================================")
        try:
            choice = input("Select mode (1/2/3, Enter = 1): ").strip()
        except EOFError:
            choice = "1"

        if choice == "2":
            n = batch_repack(FHM_UNPACKED_DIR, FHM_OUT_DIR)
            print(f"\nDone! Repacked .fhm containers: {n} (output in '{FHM_OUT_DIR}').")
        elif choice == "3":
            g_cnt, p_cnt = batch_export_hdtex(FHM_IN_DIR, PNG_OUT_DIR)
            print(
                f"\nDone! Exported to '{PNG_OUT_DIR}':\n"
                f"  - GIM textures with hashes for acz_hdtex.dll: {g_cnt}\n"
                f"  - Dynamic fonts with hashes for acz_hdtex.dll (pausetxt_pmenu/): {p_cnt}"
            )
        else:
            n = batch_unpack(FHM_IN_DIR, FHM_UNPACKED_DIR)
            if n == 0:
                print(
                    f"No .fhm files found in '{FHM_IN_DIR}'.\n"
                    f"Place your .fhm files in '{FHM_IN_DIR}' and run the script again."
                )
            else:
                print(f"\nDone! Unpacked .fhm containers: {n} (output in '{FHM_UNPACKED_DIR}').")
        return

    cmd = args[0].lower()
    if cmd == "unpack":
        in_p = args[1] if len(args) > 1 else FHM_IN_DIR
        out_p = args[2] if len(args) > 2 else FHM_UNPACKED_DIR
        n = batch_unpack(in_p, out_p)
        print(f"\nDone! Unpacked .fhm containers: {n}.")
    elif cmd == "repack":
        in_p = args[1] if len(args) > 1 else FHM_UNPACKED_DIR
        out_p = args[2] if len(args) > 2 else FHM_OUT_DIR
        n = batch_repack(in_p, out_p)
        print(f"\nDone! Repacked .fhm containers: {n}.")
    elif cmd in ("export_hdtex", "export"):
        in_p = args[1] if len(args) > 1 else FHM_IN_DIR
        out_p = args[2] if len(args) > 2 else PNG_OUT_DIR
        g_cnt, p_cnt = batch_export_hdtex(in_p, out_p)
        print(
            f"\nDone! Exported to '{out_p}':\n"
            f"  - GIM textures with hashes for acz_hdtex.dll: {g_cnt}\n"
            f"  - Dynamic fonts with hashes for acz_hdtex.dll (pausetxt_pmenu/): {p_cnt}"
        )
    else:
        in_p = args[0]
        out_p = args[1] if len(args) > 1 else FHM_UNPACKED_DIR
        n = batch_unpack(in_p, out_p)
        print(f"\nDone! Unpacked .fhm containers: {n}.")


if __name__ == "__main__":
    main()