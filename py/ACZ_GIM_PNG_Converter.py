#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ace Combat 5 / Zero Direct GIM <-> PNG Converter (v5 — 4-Bit & 8-Bit RGBA + HDTex Hash)
=======================================================================================
Converts Ace Combat 5 / Zero .gim textures directly to 32-bit RGBA .png and back to .gim.

Features:
  - Supports both 4-bit (16-color, PSMT4) and 8-bit (256-color, PSMT8) indexed GIMs.
  - Full 32-bit RGBA color and alpha preservation (HUD, fonts, specular maps).
  - Automatic PS2 CSM1 palette swizzling for 8-bit textures and linear layout for 4-bit.
  - Automatic calculation of the exact 64-bit FNV-1a hash used by acz_hdtex.dll.
  - Outputs PNGs named <filename>_<16_HEX_HASH>.png ready for mods/textures/.
  - Converts modified PNGs back to 4-bit or 8-bit .gim using a reference file or auto-mode.

Version History:
  - v1: Initial 8-bit GIM to PNG converter.
  - v2: Added PS2 CSM1 CLUT palette unswizzling/swizzling for 8-bit indexed textures.
  - v3: Added full 32-bit RGBA alpha scaling (PS2 0..128 <-> PC 0..255) and custom
        median-cut RGBA quantizer (quantize_rgba) for PNG -> GIM conversion.
  - v4: Added 4-bit (PSMT4, 16-color) GIM support alongside 8-bit (PSMT8, 256-color)
        and reference .gim header preservation.
  - v5: Added automatic 64-bit FNV-1a hash calculation (calc_gim_fnv1a64 with byte 0x12
        zeroed and PSM 0x1B -> 0x13 normalized) matching acz_hdtex.dll (<name>_<HASH>.png).

Requirements: pip install pillow numpy
"""

import os
import struct
import sys
import numpy as np
from PIL import Image

CONVERTER_VERSION = "v5"

GIM_IN_DIR = "GIM_IN"
PNG_OUT_DIR = "PNG_OUT"
PNG_IN_DIR = "PNG_IN"
GIM_OUT_DIR = "GIM_OUT"

for d in (GIM_IN_DIR, PNG_OUT_DIR, PNG_IN_DIR, GIM_OUT_DIR):
    os.makedirs(d, exist_ok=True)

GIM_MAGIC = b"GIM\x00"


def calc_gim_fnv1a64(gim_path: str) -> str:
    """
    Calculates the exact 64-bit FNV-1a hash used by acz_hdtex.dll
    (with byte 0x12 normalized to 0 and PSM 0x1B at 0x13 normalized to 0x13).
    """
    with open(gim_path, "rb") as f:
        data = bytearray(f.read())
    if len(data) < 0x20 or data[:4] != GIM_MAGIC:
        raise ValueError("Invalid GIM file (missing GIM\\x00 magic header).")
    data[0x12] = 0
    if data[0x13] == 0x1B:
        data[0x13] = 0x13
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x00000100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016X}"


def csm1_swizzle_palette(palette: np.ndarray) -> np.ndarray:
    """
    Swaps indices 8..15 and 16..23 in every 32-color block (PS2 CSM1 CLUT format).
    This operation is its own inverse (unswizzle == swizzle).
    Used for 8-bit (256-color) textures.
    """
    out = palette.copy()
    num_colors = out.shape[0]
    for base in range(0, num_colors, 32):
        if base + 24 <= num_colors:
            out[base + 8 : base + 16] = palette[base + 16 : base + 24]
            out[base + 16 : base + 24] = palette[base + 8 : base + 16]
    return out


def read_gim(gim_path: str):
    """
    Reads an Ace Combat .gim file and returns:
    (bpp, width, height, indices_2d, linear_palette_rgba, raw_header_32, raw_clut_subheader_16)
    Supports both 4-bit (16-color) and 8-bit (256-color) indexed GIM textures.
    """
    with open(gim_path, "rb") as f:
        data = f.read()

    if len(data) < 48 or data[:4] != GIM_MAGIC:
        raise ValueError("Not a valid Ace Combat GIM file (missing GIM\\x00 magic).")

    width, height = struct.unpack_from("<HH", data, 28)
    if width == 0 or height == 0:
        raise ValueError(f"Invalid texture dimensions ({width}x{height}) in {gim_path}.")

    psm = data[0x13]
    header_32 = data[:32]

    size_4bit = 32 + ((width * height) // 2) + 16 + 64
    size_8bit = 32 + (width * height) + 16 + 1024

    if psm in (20, 36) or len(data) == size_4bit or (len(data) < size_8bit and len(data) >= size_4bit):
        bpp = 4
        pix_len = (width * height) // 2
        raw_pix = data[32 : 32 + pix_len]
        clut_subhdr = data[32 + pix_len : 32 + pix_len + 16]
        pal_data = data[32 + pix_len + 16 : 32 + pix_len + 16 + 64]
        if len(pal_data) < 64:
            raise ValueError(f"Truncated 4-bit palette in {gim_path}.")

        linear_palette = np.frombuffer(pal_data, dtype=np.uint8).reshape((16, 4)).copy()

        raw_u8 = np.frombuffer(raw_pix, dtype=np.uint8)
        p0 = raw_u8 & 0x0F
        p1 = raw_u8 >> 4
        indices = np.empty((width * height,), dtype=np.uint8)
        indices[0::2] = p0
        indices[1::2] = p1
        indices = indices.reshape((height, width))

    elif psm in (19, 27) or len(data) >= size_8bit:
        bpp = 8
        pix_len = width * height
        raw_pix = data[32 : 32 + pix_len]
        clut_subhdr = data[32 + pix_len : 32 + pix_len + 16]
        pal_data = data[32 + pix_len + 16 : 32 + pix_len + 16 + 1024]
        if len(pal_data) < 1024:
            raise ValueError(f"Truncated 8-bit palette in {gim_path}.")

        raw_palette = np.frombuffer(pal_data, dtype=np.uint8).reshape((256, 4))
        linear_palette = csm1_swizzle_palette(raw_palette)
        indices = np.frombuffer(raw_pix, dtype=np.uint8).reshape((height, width))
    else:
        raise ValueError(
            f"Unsupported GIM format (len={len(data)}, w={width}, h={height}, psm={psm})."
        )

    return bpp, width, height, indices, linear_palette, header_32, clut_subhdr


def gim_to_png(gim_path: str, png_path: str) -> bool:
    """Converts a .gim texture to a 32-bit RGBA .png, preserving the alpha channel."""
    try:
        bpp, width, height, indices, linear_palette, _, _ = read_gim(gim_path)
    except Exception as e:
        print(f"///ERROR///: Unable to read {gim_path}: {e}")
        return False

    rgba_pixels = linear_palette[indices]
    img = Image.fromarray(rgba_pixels, mode="RGBA")
    img.save(png_path, "PNG")
    return True


def quantize_rgba(rgba_pixels: np.ndarray, max_colors: int = 256):
    """
    Quantizes an HxWx4 RGBA array to at most `max_colors` unique (R, G, B, A) entries.
    Returns (indices_uint8_HxW, palette_uint8).
    """
    h, w, _ = rgba_pixels.shape
    flat = rgba_pixels.reshape(-1, 4)

    packed = (
        (flat[:, 0].astype(np.uint32) << 24)
        | (flat[:, 1].astype(np.uint32) << 16)
        | (flat[:, 2].astype(np.uint32) << 8)
        | flat[:, 3].astype(np.uint32)
    )
    unique_packed, inverse_indices = np.unique(packed, return_inverse=True)
    n_uniq = len(unique_packed)

    if n_uniq <= max_colors:
        palette = np.zeros((max_colors, 4), dtype=np.uint8)
        palette[:n_uniq, 0] = (unique_packed >> 24) & 0xFF
        palette[:n_uniq, 1] = (unique_packed >> 16) & 0xFF
        palette[:n_uniq, 2] = (unique_packed >> 8) & 0xFF
        palette[:n_uniq, 3] = unique_packed & 0xFF
        indices = inverse_indices.astype(np.uint8).reshape((h, w))
        return indices, palette

    counts = np.bincount(inverse_indices)
    u_colors = np.zeros((n_uniq, 4), dtype=np.int32)
    u_colors[:, 0] = (unique_packed >> 24) & 0xFF
    u_colors[:, 1] = (unique_packed >> 16) & 0xFF
    u_colors[:, 2] = (unique_packed >> 8) & 0xFF
    u_colors[:, 3] = unique_packed & 0xFF

    zero_alpha_mask = u_colors[:, 3] == 0
    boxes = []
    if np.any(zero_alpha_mask) and not np.all(zero_alpha_mask):
        boxes.append(np.where(zero_alpha_mask)[0])
        boxes.append(np.where(~zero_alpha_mask)[0])
    else:
        boxes.append(np.arange(n_uniq))

    while len(boxes) < max_colors:
        best_idx = -1
        best_score = -1.0
        best_axis = 0
        for i, box in enumerate(boxes):
            if len(box) <= 1:
                continue
            pts = u_colors[box]
            ranges = pts.max(axis=0) - pts.min(axis=0)
            axis = int(np.argmax(ranges))
            max_r = ranges[axis]
            if max_r == 0:
                continue
            score = float(max_r) * (np.sum(counts[box]) ** 0.25)
            if score > best_score:
                best_score = score
                best_idx = i
                best_axis = axis

        if best_idx == -1:
            break

        box = boxes.pop(best_idx)
        order = np.argsort(u_colors[box, best_axis], kind="mergesort")
        box_sorted = box[order]
        w_cum = np.cumsum(counts[box_sorted])
        mid = int(np.searchsorted(w_cum, w_cum[-1] / 2.0))
        mid = max(1, min(len(box_sorted) - 1, mid))
        boxes.append(box_sorted[:mid])
        boxes.append(box_sorted[mid:])

    palette = np.zeros((max_colors, 4), dtype=np.uint8)
    u_to_pal = np.zeros(n_uniq, dtype=np.uint8)
    for i, box in enumerate(boxes):
        w_box = counts[box][:, None]
        mean_col = np.round(np.sum(u_colors[box] * w_box, axis=0) / np.sum(w_box)).astype(np.uint8)
        palette[i] = mean_col
        u_to_pal[box] = i

    indices = u_to_pal[inverse_indices].reshape((h, w))
    return indices, palette


def png_to_gim(png_path: str, gim_out_path: str, ref_gim_path: str = None) -> bool:
    """Converts a 32-bit RGBA .png back to .gim, preserving RGBA values and palette structure."""
    try:
        img = Image.open(png_path)
    except Exception as e:
        print(f"///ERROR///: Unable to open {png_path}: {e}")
        return False

    width, height = img.size
    rgba = np.array(img.convert("RGBA"), dtype=np.uint8)

    target_bpp = 8
    header_32 = None
    clut_subhdr = None

    if ref_gim_path and os.path.exists(ref_gim_path):
        try:
            ref_bpp, _, _, _, _, ref_hdr, ref_csub = read_gim(ref_gim_path)
            target_bpp = ref_bpp
            header_32 = ref_hdr
            clut_subhdr = ref_csub
        except Exception:
            pass

    max_colors = 16 if target_bpp == 4 else 256
    indices, linear_palette = quantize_rgba(rgba, max_colors=max_colors)

    if target_bpp == 4:
        flat_idx = indices.reshape(-1)
        if len(flat_idx) % 2 != 0:
            flat_idx = np.pad(flat_idx, (0, 1), mode="constant")
        packed_bytes = ((flat_idx[0::2] & 0x0F) | ((flat_idx[1::2] & 0x0F) << 4)).astype(np.uint8).tobytes()
        final_palette_bytes = linear_palette.tobytes()

        if header_32 is None or len(header_32) != 32:
            header_32 = (
                bytearray(b"GIM\x00")
                + bytearray([205, 205, 205, 205])
                + bytearray(8)
                + bytearray([0, 0, 0, 20])  # PSMT4 (0x14 = 20)
                + bytearray(8)
                + struct.pack("<HH", width, height)
            )
        else:
            header_32 = bytearray(header_32[:28]) + struct.pack("<HH", width, height)

        if clut_subhdr is None or len(clut_subhdr) != 16:
            clut_subhdr = bytearray(12) + struct.pack("<HH", 8, 2)

    else:
        packed_bytes = indices.tobytes()
        swizzled_palette = csm1_swizzle_palette(linear_palette)
        final_palette_bytes = swizzled_palette.tobytes()

        if header_32 is None or len(header_32) != 32:
            header_32 = (
                bytearray(b"GIM\x00")
                + bytearray([205, 205, 205, 205])
                + bytearray(8)
                + bytearray([0, 0, 0, 27])  # PSMT8
                + bytearray(8)
                + struct.pack("<HH", width, height)
            )
        else:
            header_32 = bytearray(header_32[:28]) + struct.pack("<HH", width, height)

        if clut_subhdr is None or len(clut_subhdr) != 16:
            clut_subhdr = bytearray(12) + struct.pack("<HH", 16, 16)

    with open(gim_out_path, "wb") as f:
        f.write(header_32)
        f.write(packed_bytes)
        f.write(clut_subhdr)
        f.write(final_palette_bytes)

    return True


def batch_gim_to_png():
    files = [f for f in sorted(os.listdir(GIM_IN_DIR)) if f.lower().endswith(".gim")]
    if not files:
        print(f"///INFO///: Folder '{GIM_IN_DIR}' is empty. Put .gim files there first.")
        return
    ok = 0
    for f in files:
        in_p = os.path.join(GIM_IN_DIR, f)
        try:
            bpp, w, h, _, _, _, _ = read_gim(in_p)
            gim_hash = calc_gim_fnv1a64(in_p)
            base_name = os.path.splitext(f)[0]
            out_p = os.path.join(PNG_OUT_DIR, f"{base_name}_{gim_hash}.png")
            if gim_to_png(in_p, out_p):
                ok += 1
                print(f"  [OK] {f} ({bpp}-bit, {w}x{h}) -> {out_p} (Hash: {gim_hash})")
        except Exception as e:
            print(f"  [ERROR] {f}: {e}")
    print(f"///INFO///: Converted {ok}/{len(files)} GIM files to '{PNG_OUT_DIR}'.\n")


def batch_png_to_gim():
    files = [f for f in sorted(os.listdir(PNG_IN_DIR)) if f.lower().endswith(".png")]
    if not files:
        print(f"///INFO///: Folder '{PNG_IN_DIR}' is empty. Put edited .png files there first.")
        return
    ok = 0
    for f in files:
        base = os.path.splitext(f)[0]
        clean_base = base
        parts = base.rsplit("_", 1)
        if len(parts) == 2 and len(parts[1]) == 16:
            clean_base = parts[0]

        in_p = os.path.join(PNG_IN_DIR, f)
        out_p = os.path.join(GIM_OUT_DIR, clean_base + ".gim")

        ref_p = None
        for cand in (clean_base + ".gim", base + ".gim"):
            p_cand = os.path.join(GIM_IN_DIR, cand)
            if os.path.exists(p_cand):
                ref_p = p_cand
                break

        if png_to_gim(in_p, out_p, ref_p):
            ok += 1
            print(f"  [OK] {f} -> {out_p}" + (f" (using ref: {os.path.basename(ref_p)})" if ref_p else ""))
    print(f"///INFO///: Converted {ok}/{len(files)} PNG files to '{GIM_OUT_DIR}'.\n")


def main():
    if len(sys.argv) > 1:
        mode = sys.argv[1].strip().lower()
        if mode in ("1", "gim2png", "to_png"):
            batch_gim_to_png()
            return
        elif mode in ("2", "png2gim", "to_gim"):
            batch_png_to_gim()
            return

    print("==========================================================")
    print(f"   ACE COMBAT 5 / ZERO — DIRECT GIM <-> PNG CONVERTER ({CONVERTER_VERSION})")
    print("           (Supports 4-bit, 8-bit & HDTex Hashes)         ")
    print("==========================================================")
    print(f"  [1] Convert GIM -> PNG + Hash (from '{GIM_IN_DIR}' to '{PNG_OUT_DIR}')")
    print(f"  [2] Convert PNG -> GIM        (from '{PNG_IN_DIR}' to '{GIM_OUT_DIR}')")
    print("==========================================================")
    choice = input("Select mode (1 or 2): ").strip()
    print()
    if choice == "1":
        batch_gim_to_png()
    elif choice == "2":
        batch_png_to_gim()
    else:
        print("///INFO///: Unknown selection. Exiting.")

    input("///INFO///: Press Enter to exit...")


if __name__ == "__main__":
    main()