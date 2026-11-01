#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ace Combat Zero: The Belkan War — Universal TIM2 (.tm2 / .swa SWAD) Scanner & Converter (v1)
============================================================================================
Scans unpacked game archives (a.arc, d.arc, data/select/*.swa, etc.) and converts all standalone
and embedded PS2 TIM2 ("TIM2") textures from any container format (.swa SWAD archives, .fhm,
.msa, .bin, .dat, .pac, .tm2, .tim2) into 32-bit RGBA PNGs + companion .json metadata ready
for direct HD replacement via acz_hdtex.dll (v0.16+).

Key Features:
  1. Complete Container & Stream Coverage:
     - Scans all .SWA (SWAD Flash/menu archives such as AircraftSelect.swa, Shop.swa, MainSelect.swa),
       .FHM, .MSA, .BIN, .DAT, .PAC, .TM2, and .TIM2 files.
     - Detects TIM2 textures at arbitrary 16-byte-aligned offsets inside binary streams and nested
       FHM/SWAD containers.
     - Supports both 16-byte (align=0) and 128-byte (align=1) TIM2 file headers, standard 48-byte
       (0x30) picture headers, and 80-byte (0x50) extended headers with "eXt\\0" + "GIDX" tags.
  2. Accurate PS2 Color, Alpha & Palette Decoding:
     - Supports 4-bit (IDTEX4 / PSMT4, 16 colors), 8-bit (IDTEX8 / PSMT8, 256 colors with automatic
       PS2 CSM1 palette unswizzling), 16-bit (RGB16), 24-bit (RGB24), and 32-bit (RGB32) TIM2 formats.
     - Converts PS2 GS alpha (0..128 -> 0..255) using the exact formula `(a * 255 + 64) // 128`.
  3. Triple-Hash & Non-Power-of-Two Sub-Region Metadata (.json) for acz_hdtex.dll v0.16:
     - Computes all three 64-bit FNV-1a hashes recognized by acz_hdtex.dll v0.16:
         * `hash_tim2_norm`     : Full canonical TIM2 file hash (primary hash)
         * `hash_tim2_pix_clut` : Pixel + CLUT payload hash
         * `hash_tim2_pixels`   : Raw pixel payload hash (for scratch VRAM / stripped uploads)
     - Writes a companion `<stem>.json` containing `"width"`, `"height"`, and `"hashes"` so that
       non-power-of-two TIM2 sprites (e.g. 40x40 Hangar weapon icons allocated in 64x64 GS slots)
       are automatically padded to power-of-two UV canvases in RAM by acz_hdtex.dll v0.16.
  4. Organized Output & Deduplication:
     - `TIM2_SCAN_OUT/ready_for_hdtex/`: Unique deduplicated TIM2 textures across all scanned files,
       named `<HASH>_tim2_<WxH>_psm<PSM>.png` (+ `.json`), ready to edit/upscale and drop into `mods/textures/`.
     - `TIM2_SCAN_OUT/by_container/<container_name>/`: Per-file breakdown (when `--by-container` is passed
       or by default alongside `ready_for_hdtex`).
     - `TIM2_SCAN_OUT/tim2_hash_catalog.txt`: Complete human-readable catalog of every discovered TIM2
       texture, its dimensions, format, all 3 hashes, and all archive files where it appears.

Usage:
  python ACZ_TIM2_Scanner.py [input_dir_or_file] [output_dir] [options]

Options:
  --no-by-container   Only output deduplicated textures in ready_for_hdtex/ (skip by_container/ copies)
  --save-raw-tm2      Also export raw .tm2 binary files alongside .png and .json
  --min-size WxH      Minimum texture width/height filter (e.g. --min-size 16x16)

Version History:
  - v1: Initial release. Scans standalone and embedded PS2 TIM2 textures across .swa
        (SWAD menu archives), .fhm, .msa, .bin, .dat, .pac, and .tm2 files; decodes
        4-bit/8-bit/16-bit/24-bit/32-bit TIM2s with PS2 alpha scaling and CSM1 CLUT
        unswizzling; outputs RGBA PNGs + companion .json files with triple FNV-1a
        hashes (hash_tim2_norm, hash_tim2_pix_clut, hash_tim2_pixels) and native
        dimensions (width, height) for acz_hdtex.dll v0.16+.
"""

import argparse
import json
import os
import shutil
import struct
import sys
from typing import Dict, List, Optional, Tuple

import numpy as np
from PIL import Image

SCANNER_VERSION = "v1"
DEFAULT_EXTRACTED_DIR = "extracted"
DEFAULT_TIM2_OUT_DIR = "TIM2_SCAN_OUT"
TIM2_MAGIC = b"TIM2"

FNV1A_64_OFFSET = 0xCBF29CE484222325
FNV1A_64_PRIME = 0x100000001B3

SKIP_NON_RESOURCE_EXTENSIONS = {
    ".txt",
    ".log",
    ".py",
    ".pyc",
    ".cpp",
    ".c",
    ".h",
    ".hpp",
    ".ini",
    ".cfg",
    ".json",
    ".xml",
    ".yaml",
    ".yml",
    ".md",
    ".bat",
    ".cmd",
    ".sh",
    ".ps1",
    ".dll",
    ".exe",
    ".pdb",
    ".obj",
    ".lib",
    ".exp",
    ".ilk",
    ".sln",
    ".vcxproj",
    ".filters",
    ".user",
    ".7z",
    ".zip",
    ".rar",
    ".tar",
    ".gz",
    ".xz",
    ".wav",
    ".mp3",
    ".ogg",
    ".flac",
    ".adx",
    ".afs",
    ".sfd",
    ".pss",
    ".mpg",
    ".mpeg",
    ".mp4",
    ".avi",
    ".mkv",
    ".dmp",
    ".png",
    ".jpg",
    ".jpeg",
    ".bmp",
    ".gim",
}


def fnv1a64_bytes(buf: bytes) -> int:
    """Computes standard 64-bit FNV-1a hash over a byte buffer."""
    h = FNV1A_64_OFFSET
    prime = FNV1A_64_PRIME
    for b in buf:
        h = ((h ^ b) * prime) & 0xFFFFFFFFFFFFFFFF
    return h


def ps2_alpha_to_pc(a_ps2: np.ndarray) -> np.ndarray:
    """Exact integer conversion of PS2 alpha (0..128 -> 0..255), matching Ps2AlphaToPc in acz_hdtex.cpp."""
    v = (a_ps2.astype(np.uint32) * 255 + 64) // 128
    return np.clip(v, 0, 255).astype(np.uint8)


def csm1_swizzle_palette(palette: np.ndarray) -> np.ndarray:
    """PS2 GS CSM1 CLUT block permutation (for 8-bit IDTEX8 / PSMT8 textures when CSM1 bit is active)."""
    out = palette.copy()
    num_colors = len(out)
    for base in range(0, num_colors, 32):
        if base + 24 <= num_colors:
            out[base + 8 : base + 16] = palette[base + 16 : base + 24]
            out[base + 16 : base + 24] = palette[base + 8 : base + 16]
    return out


def try_parse_tim2_at(buf: bytes, offset: int = 0) -> Optional[dict]:
    """
    Checks whether a valid PS2 TIM2 texture starts at `offset` in `buf`.
    Returns a metadata dictionary (with offsets and sizes) or None if not a valid TIM2.
    """
    buf_len = len(buf)
    if offset < 0 or offset + 0x40 > buf_len:
        return None
    if buf[offset : offset + 4] != TIM2_MAGIC:
        return None

    ver = buf[offset + 4]
    align = buf[offset + 5]
    pic_count = struct.unpack_from("<H", buf, offset + 6)[0]
    if ver != 4 or align not in (0, 1) or pic_count == 0 or pic_count > 64:
        return None

    pic_offset = 16 if align == 0 else 128
    if offset + pic_offset + 48 > buf_len:
        return None

    pic_base = offset + pic_offset
    total_pic_size, clut_size, image_size, header_size, clut_colors = struct.unpack_from("<IIIHH", buf, pic_base)
    pict_format, mipmap_lv, clut_type, image_type, width, height = struct.unpack_from("<BBBBHH", buf, pic_base + 16)

    if pict_format != 0 or mipmap_lv == 0 or mipmap_lv > 8:
        return None
    if header_size < 48 or header_size > 4096 or (header_size & 0xF) != 0:
        return None
    if width == 0 or height == 0 or width > 4096 or height > 4096:
        return None
    if image_size == 0 or image_size > 16 * 1024 * 1024:
        return None
    if total_pic_size != header_size + image_size + clut_size:
        return None

    total_size = pic_offset + total_pic_size
    if offset + total_size > buf_len:
        return None

    if image_type == 4:  # 4-bit IDTEX4 (PSMT4)
        expected_img_sz = (width * height) // 2
        bpp = 4
        psm = 0x14
        if clut_size not in (32, 64) or image_size < expected_img_sz:
            return None
    elif image_type == 5:  # 8-bit IDTEX8 (PSMT8)
        expected_img_sz = width * height
        bpp = 8
        psm = 0x13
        if clut_size not in (512, 1024) or image_size < expected_img_sz:
            return None
    elif image_type == 3:  # 32-bit RGB32 (PSMCT32)
        expected_img_sz = width * height * 4
        bpp = 32
        psm = 0x00
        if image_size < expected_img_sz:
            return None
    elif image_type == 2:  # 24-bit RGB24 (PSMCT24)
        expected_img_sz = width * height * 3
        bpp = 24
        psm = 0x01
        if image_size < expected_img_sz:
            return None
    elif image_type == 1:  # 16-bit RGB16 (PSMCT16)
        expected_img_sz = width * height * 2
        bpp = 16
        psm = 0x02
        if image_size < expected_img_sz:
            return None
    else:
        return None

    has_ext = False
    if header_size >= 0x50:
        ext_magic = buf[pic_base + 0x30 : pic_base + 0x34]
        gidx_magic = buf[pic_base + 0x40 : pic_base + 0x44]
        has_ext = (ext_magic == b"eXt\x00" and gidx_magic == b"GIDX")

    return {
        "offset": offset,
        "pic_offset": pic_offset,
        "header_size": header_size,
        "image_size": image_size,
        "clut_size": clut_size,
        "total_pic_size": total_pic_size,
        "total_size": total_size,
        "clut_colors": clut_colors,
        "clut_type": clut_type,
        "image_type": image_type,
        "bpp": bpp,
        "psm": psm,
        "width": width,
        "height": height,
        "has_ext": has_ext,
    }


def decode_tim2_entry(buf: bytes, info: dict) -> Tuple[Image.Image, dict]:
    """
    Decodes a validated TIM2 entry from `buf` into a 32-bit RGBA PIL.Image
    and computes all three 64-bit FNV-1a hashes matching acz_hdtex.dll v0.16.
    """
    off = info["offset"]
    pic_off = info["pic_offset"]
    hdr_sz = info["header_size"]
    img_sz = info["image_size"]
    clut_sz = info["clut_size"]
    tot_sz = info["total_size"]
    w = info["width"]
    h = info["height"]
    img_type = info["image_type"]
    clut_type = info["clut_type"]

    raw_tim2 = buf[off : off + tot_sz]
    pix_start = off + pic_off + hdr_sz
    pix_bytes = buf[pix_start : pix_start + img_sz]
    clut_bytes = buf[pix_start + img_sz : pix_start + img_sz + clut_sz]

    h_norm = fnv1a64_bytes(raw_tim2)
    h_pix_clut = fnv1a64_bytes(pix_bytes + clut_bytes)
    h_pix = fnv1a64_bytes(pix_bytes)

    if img_type in (4, 5):
        num_colors = 256 if img_type == 5 else 16
        if (clut_type & 0x07) == 1:  # 16-bit BGR5A1 CLUT
            u16 = np.frombuffer(clut_bytes[: num_colors * 2], dtype="<u2")
            pal = np.zeros((num_colors, 4), dtype=np.uint8)
            pal[:, 0] = ((u16 & 0x1F) * 255) // 31
            pal[:, 1] = (((u16 >> 5) & 0x1F) * 255) // 31
            pal[:, 2] = (((u16 >> 10) & 0x1F) * 255) // 31
            pal[:, 3] = np.where((u16 & 0x8000) != 0, 255, 0).astype(np.uint8)
        else:  # 32-bit RGBA CLUT (PS2 alpha 0..128)
            pal = np.frombuffer(clut_bytes[: num_colors * 4], dtype=np.uint8).reshape(num_colors, 4).copy()
            pal[:, 3] = ps2_alpha_to_pc(pal[:, 3])

        if num_colors == 256 and (clut_type & 0x80) == 0:
            pal = csm1_swizzle_palette(pal)

        raw_u8 = np.frombuffer(pix_bytes, dtype=np.uint8)
        if img_type == 5:
            indices = raw_u8[: w * h].reshape((h, w))
        else:
            low = raw_u8 & 0x0F
            high = (raw_u8 >> 4) & 0x0F
            unp = np.empty(len(raw_u8) * 2, dtype=np.uint8)
            unp[0::2] = low
            unp[1::2] = high
            indices = unp[: w * h].reshape((h, w))

        rgba = pal[indices]
    elif img_type == 3:  # 32-bit RGBA
        rgba = np.frombuffer(pix_bytes[: w * h * 4], dtype=np.uint8).reshape((h, w, 4)).copy()
        rgba[:, :, 3] = ps2_alpha_to_pc(rgba[:, :, 3])
    elif img_type == 2:  # 24-bit RGB
        rgb = np.frombuffer(pix_bytes[: w * h * 3], dtype=np.uint8).reshape((h, w, 3))
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:, :, :3] = rgb
        rgba[:, :, 3] = 255
    else:  # 16-bit BGR5A1
        u16 = np.frombuffer(pix_bytes[: w * h * 2], dtype="<u2").reshape((h, w))
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:, :, 0] = ((u16 & 0x1F) * 255) // 31
        rgba[:, :, 1] = (((u16 >> 5) & 0x1F) * 255) // 31
        rgba[:, :, 2] = (((u16 >> 10) & 0x1F) * 255) // 31
        rgba[:, :, 3] = np.where((u16 & 0x8000) != 0, 255, 0).astype(np.uint8)

    img = Image.fromarray(rgba, mode="RGBA")
    meta = dict(info)
    meta["hash_tim2_norm"] = f"{h_norm:016X}"
    meta["hash_tim2_pix_clut"] = f"{h_pix_clut:016X}"
    meta["hash_tim2_pixels"] = f"{h_pix:016X}"
    meta["raw_tim2"] = raw_tim2
    return img, meta


def scan_buffer_for_tim2(buf: bytes) -> List[Tuple[Image.Image, dict]]:
    """Scans an arbitrary binary buffer for all embedded or standalone TIM2 textures."""
    results: List[Tuple[Image.Image, dict]] = []
    pos = 0
    buf_len = len(buf)
    while pos <= buf_len - 0x40:
        idx = buf.find(TIM2_MAGIC, pos)
        if idx == -1:
            break
        info = try_parse_tim2_at(buf, idx)
        if info is not None:
            try:
                img, meta = decode_tim2_entry(buf, info)
                results.append((img, meta))
                pos = idx + info["total_size"]
                continue
            except Exception:
                pass
        pos = idx + 4
    return results


def run_tim2_scanner(
    input_path: str,
    output_root: str,
    by_container: bool = True,
    save_raw_tm2: bool = False,
    min_w: int = 1,
    min_h: int = 1,
) -> None:
    if not os.path.exists(input_path):
        print(f"[ERROR] Input path '{input_path}' not found!")
        print("Usage: python ACZ_TIM2_Scanner.py <extracted_dir_or_file> [output_dir]")
        return

    ready_dir = os.path.join(output_root, "ready_for_hdtex")
    container_root = os.path.join(output_root, "by_container")
    os.makedirs(ready_dir, exist_ok=True)
    if by_container:
        os.makedirs(container_root, exist_ok=True)

    candidate_files: List[Tuple[str, str]] = []
    if os.path.isfile(input_path):
        candidate_files.append((input_path, os.path.basename(input_path)))
    else:
        for root, _, files in os.walk(input_path):
            for fn in sorted(files):
                _, ext = os.path.splitext(fn)
                if ext.lower() in SKIP_NON_RESOURCE_EXTENSIONS:
                    continue
                full_p = os.path.join(root, fn)
                rel_p = os.path.relpath(full_p, input_path)
                candidate_files.append((full_p, rel_p))

    print("=" * 80)
    print(f" ACZ Universal TIM2 (.tm2 / .swa SWAD) Scanner & Converter ({SCANNER_VERSION})")
    print("=" * 80)
    print(f"[*] Scanning source : {os.path.abspath(input_path)} ({len(candidate_files)} candidate files)")
    print(f"[*] Output folder   : {os.path.abspath(output_root)}")
    print("-" * 80)

    unique_tim2: Dict[str, dict] = {}
    total_instances = 0
    files_with_tim2 = 0

    for full_p, rel_p in candidate_files:
        try:
            fsize = os.path.getsize(full_p)
            if fsize < 0x40 or fsize > 128 * 1024 * 1024:
                continue
            with open(full_p, "rb") as f:
                data = f.read()
            if TIM2_MAGIC not in data:
                continue

            found = scan_buffer_for_tim2(data)
            if not found:
                continue

            kept = [(im, m) for (im, m) in found if m["width"] >= min_w and m["height"] >= min_h]
            if not kept:
                continue

            files_with_tim2 += 1
            rel_clean = rel_p.replace("\\", "/")
            c_stem = os.path.splitext(rel_clean)[0].replace("/", "__")
            c_dir = os.path.join(container_root, c_stem) if by_container else ""
            if by_container:
                os.makedirs(c_dir, exist_ok=True)

            print(f"  [+] {rel_clean}: {len(kept)} TIM2 texture(s)")

            for idx_in_file, (img, meta) in enumerate(kept):
                total_instances += 1
                h_norm = meta["hash_tim2_norm"]
                h_pix_clut = meta["hash_tim2_pix_clut"]
                h_pix = meta["hash_tim2_pixels"]
                w, h = meta["width"], meta["height"]
                psm = meta["psm"]
                bpp = meta["bpp"]
                off = meta["offset"]

                loc_str = f"{rel_clean} [#{idx_in_file:03d} @ 0x{off:X}]"
                hashes_list = [h_norm]
                if h_pix_clut not in hashes_list:
                    hashes_list.append(h_pix_clut)
                if h_pix not in hashes_list:
                    hashes_list.append(h_pix)

                if by_container:
                    c_base = f"entry_{idx_in_file:03d}_{w}x{h}_psm{psm:02X}_{h_norm}"
                    c_png = os.path.join(c_dir, c_base + ".png")
                    c_json = os.path.join(c_dir, c_base + ".json")
                    img.save(c_png, "PNG")
                    with open(c_json, "w", encoding="utf-8", newline="\n") as jf:
                        json.dump(
                            {
                                "comment": f"TIM2 from {loc_str}",
                                "width": w,
                                "height": h,
                                "bpp": bpp,
                                "psm": f"0x{psm:02X}",
                                "hashes": hashes_list,
                            },
                            jf,
                            indent=2,
                        )
                    if save_raw_tm2:
                        with open(os.path.join(c_dir, c_base + ".tm2"), "wb") as tf:
                            tf.write(meta["raw_tim2"])

                if h_norm not in unique_tim2:
                    u_base = f"{h_norm}_tim2_{w}x{h}_psm{psm:02X}"
                    u_png = os.path.join(ready_dir, u_base + ".png")
                    img.save(u_png, "PNG")
                    if save_raw_tm2:
                        with open(os.path.join(ready_dir, u_base + ".tm2"), "wb") as tf:
                            tf.write(meta["raw_tim2"])
                    unique_tim2[h_norm] = {
                        "base_name": u_base,
                        "width": w,
                        "height": h,
                        "bpp": bpp,
                        "psm": psm,
                        "hashes": hashes_list,
                        "sources": [loc_str],
                    }
                else:
                    u_entry = unique_tim2[h_norm]
                    for hx in hashes_list:
                        if hx not in u_entry["hashes"]:
                            u_entry["hashes"].append(hx)
                    if loc_str not in u_entry["sources"]:
                        u_entry["sources"].append(loc_str)

        except Exception as exc:
            print(f"  [WARN] Error scanning {rel_p}: {exc}")

    # Write companion .json files for all unique deduplicated TIM2 textures in ready_for_hdtex/
    for h_norm, u_entry in sorted(unique_tim2.items()):
        u_json = os.path.join(ready_dir, u_entry["base_name"] + ".json")
        with open(u_json, "w", encoding="utf-8", newline="\n") as jf:
            json.dump(
                {
                    "comment": f"TIM2 {u_entry['width']}x{u_entry['height']} ({u_entry['bpp']}bpp, used in {len(u_entry['sources'])} location(s))",
                    "width": u_entry["width"],
                    "height": u_entry["height"],
                    "hashes": u_entry["hashes"],
                    "sources": u_entry["sources"][:25],
                },
                jf,
                indent=2,
            )

    catalog_path = os.path.join(output_root, "tim2_hash_catalog.txt")
    with open(catalog_path, "w", encoding="utf-8", newline="\n") as cf:
        cf.write("=" * 110 + "\n")
        cf.write(f" ACE COMBAT ZERO — TIM2 TEXTURE HASH CATALOG ({SCANNER_VERSION})\n")
        cf.write("=" * 110 + "\n")
        cf.write(
            f" Total Files with TIM2: {files_with_tim2} | Total TIM2 Instances: {total_instances} | Unique TIM2 Textures: {len(unique_tim2)}\n"
        )
        cf.write("-" * 110 + "\n")
        cf.write("PRIMARY HASH     | PIXEL HASH       | DIMS      | BPP  | PSM    | COUNT | SAMPLE SOURCE LOCATION\n")
        cf.write("-" * 110 + "\n")
        for h_norm, u_entry in sorted(
            unique_tim2.items(), key=lambda kv: (kv[1]["sources"][0], kv[0])
        ):
            h_pix = u_entry["hashes"][-1]
            dims = f"{u_entry['width']}x{u_entry['height']}"
            src0 = u_entry["sources"][0]
            cf.write(
                f"{h_norm} | {h_pix} | {dims:<9s} | {u_entry['bpp']:2d}b  | 0x{u_entry['psm']:02X}   | "
                f"{len(u_entry['sources']):5d} | {src0}\n"
            )

    print("-" * 80)
    print(f"[SUCCESS] TIM2 Scan Complete ({SCANNER_VERSION})!")
    print(f"  - Containers with TIM2   : {files_with_tim2}")
    print(f"  - Total TIM2 instances   : {total_instances}")
    print(f"  - Unique TIM2 textures   : {len(unique_tim2)} -> {ready_dir}")
    if by_container:
        print(f"  - Per-container folders  : {container_root}")
    print(f"  - Full TIM2 hash catalog : {catalog_path}")
    print("=" * 80)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=f"Ace Combat Zero Universal TIM2 Scanner & Converter ({SCANNER_VERSION})."
    )
    parser.add_argument(
        "input_path",
        nargs="?",
        default=DEFAULT_EXTRACTED_DIR,
        help=f"Path to unpacked game archive directory or .swa/.fhm file (default: {DEFAULT_EXTRACTED_DIR})",
    )
    parser.add_argument(
        "out_dir",
        nargs="?",
        default=DEFAULT_TIM2_OUT_DIR,
        help=f"Output directory for extracted TIM2 PNGs + JSONs (default: {DEFAULT_TIM2_OUT_DIR})",
    )
    parser.add_argument(
        "--no-by-container",
        action="store_true",
        help="Only output deduplicated textures in ready_for_hdtex/ (skip by_container/ copies)",
    )
    parser.add_argument(
        "--save-raw-tm2",
        action="store_true",
        help="Also export raw .tm2 binary files alongside .png and .json",
    )
    parser.add_argument(
        "--min-size",
        default="1x1",
        help="Minimum texture dimension filter WxH, e.g. --min-size 16x16",
    )
    args = parser.parse_args()

    min_w, min_h = 1, 1
    if "x" in args.min_size.lower():
        parts = args.min_size.lower().split("x", 1)
        try:
            min_w, min_h = int(parts[0]), int(parts[1])
        except ValueError:
            pass

    run_tim2_scanner(
        input_path=args.input_path,
        output_root=args.out_dir,
        by_container=not args.no_by_container,
        save_raw_tm2=args.save_raw_tm2,
        min_w=min_w,
        min_h=min_h,
    )


if __name__ == "__main__":
    main()
