#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ACZ_SWG_Visual_Editor.py — Ace Combat Zero (PC Recompilation)
Full WYSIWYG Visual Editor for SWAD (.swa), SWG (.swg), and MSA (select.msa) Menus (v2.1).

Features (v2.1):
  1. Real In-Game Texture & Scene Compositor (via Pillow) with PS2 GS Additive Blending:
     - Decodes embedded PS2 TIM2 textures (4-bit indexed, 8-bit CSM1 unswizzled, 32-bit RGBA)
       directly from .swa chunks.
     - Parses Table 2 (`0x64`) blend flags (`0` = Standard Alpha Blend, `1` = PS2 GS Additive Blend)
       so glowing sparks, cockpit overlays, scanlines, and highlight flares add light (`ImageChops.add`)
       instead of darkening the background.
     - Properly carries forward display lists across Hold Frames (`opcnt == 0`) so scrubbing any
       frame on the timeline never drops textures.
     - Smart Main Timeline MovieClip (`main_mc_id`) detection across all 31 `.swa` files, plus an
       interactive MovieClip (`MC:`) selector in the toolbar to inspect any MovieClip in the file.
     - Automatically loads localized strings from `select.msa` (MSAD + SHT + 8 variable-width
       font string pools) and renders them onto Type 3 TextFields (`adress`, `but`, `msg`,
       menu buttons) at their exact in-game positions and colors.
  2. Interactive Zoom & Pan + Move & 8-Handle Edge/Corner Resize Gizmo on Canvas:
     - Zoom from 50% to 300% via toolbar buttons (`-` / `+`), Zoom dropdown, or MouseWheel
       (`<MouseWheel>` / `<Button-4>` / `<Button-5>`).
     - Pan the canvas freely by dragging with the Right Mouse Button or Middle Mouse Button.
     - Click any element on the canvas or in the hierarchical Scene Tree to select it.
     - Drag inside the selection box with Left Mouse Button to move (`tx, ty` in Table 0).
     - Drag the 8 yellow edge/corner handles (`N, S, E, W, NW, NE, SW, SE`) to stretch or squeeze
       the element horizontally/vertically (`m00 / m11` in Table 0).
  3. Named Frame Labels & Timeline Scrubber:
     - Jump directly to any menu state (`main`, `cmp_main`, `opt_main`, `opt_disSet`, `opt_sRatio`, etc.)
       or scrub frame-by-frame across the timeline.
  4. Built-in Text & `select.msa` Localization Editor Tab:
     - Inspect and edit all `W...`, `B...`, `H...`, `M...` strings referenced by the open menu
       (or search all 4,636 strings in `select.msa`), preview changes live on the canvas, and
       repack modified strings back into `select.msa`.
"""

import os
import sys
import struct
from typing import Dict, List, Tuple, Optional
from PIL import Image, ImageDraw, ImageFont, ImageChops

SWAD_MAGIC = b"SWAD"
SWG_MAGIC  = b"SWG\x00"
MSAD_MAGIC = b"MSAD"


# ============================================================================
# 1. PS2 TIM2 Texture Decoder (4-bit, 8-bit CSM1 Unswizzled, 32-bit RGBA)
# ============================================================================

def unswizzle_clut_8bit(clut_data: bytes) -> List[Tuple[int, int, int, int]]:
    colors = [struct.unpack_from("4B", clut_data, i * 4) for i in range(256)]
    unswizzled = [None] * 256
    for i in range(256):
        block = i // 32
        sub = i % 32
        if 8 <= sub < 16:
            orig = block * 32 + sub + 8
        elif 16 <= sub < 24:
            orig = block * 32 + sub - 8
        else:
            orig = i
        unswizzled[i] = colors[orig]
    return unswizzled


def decode_tim2_to_pil(data: bytes) -> Optional[Image.Image]:
    if len(data) < 48 or data[:4] != b"TIM2":
        return None
    total_sz, clut_sz, img_sz = struct.unpack_from("<III", data, 16)
    hdr_sz, clut_colors = struct.unpack_from("<HH", data, 28)
    pict_fmt, mipmap_cnt, clut_type, img_type = struct.unpack_from("<BBBB", data, 32)
    w, h = struct.unpack_from("<HH", data, 36)
    if w == 0 or h == 0:
        return None

    img_off = 16 + hdr_sz
    clut_off = img_off + img_sz
    if img_off + img_sz > len(data):
        return None

    if img_type == 4:  # 4-bit indexed (16 colors)
        if clut_off + 64 > len(data):
            return None
        palette = []
        for i in range(16):
            r, g, b, a = struct.unpack_from("4B", data, clut_off + i * 4)
            a = min(255, a * 2) if a <= 128 else a
            palette.append((r, g, b, a))
        raw = data[img_off:img_off + img_sz]
        pix = bytearray(w * h * 4)
        for idx in range(w * h):
            b_val = raw[idx // 2]
            p_idx = (b_val & 0x0F) if (idx % 2 == 0) else ((b_val >> 4) & 0x0F)
            pix[idx * 4:(idx + 1) * 4] = struct.pack("4B", *palette[p_idx])
        return Image.frombytes("RGBA", (w, h), bytes(pix))

    elif img_type == 5:  # 8-bit indexed (256 colors)
        if clut_off + clut_sz > len(data):
            return None
        raw_clut = data[clut_off:clut_off + clut_sz]
        if (clut_type & 0x80) == 0 and len(raw_clut) >= 1024:
            clut = unswizzle_clut_8bit(raw_clut)
        else:
            clut = [struct.unpack_from("4B", raw_clut, i * 4) for i in range(min(256, len(raw_clut) // 4))]
        raw = data[img_off:img_off + img_sz]
        lut = [b""] * 256
        for i, (r, g, b, a) in enumerate(clut):
            a_scaled = min(255, a * 2) if a <= 128 else a
            lut[i] = struct.pack("4B", r, g, b, a_scaled)
        pix = b"".join(lut[b_val] for b_val in raw[:w * h])
        return Image.frombytes("RGBA", (w, h), pix)

    elif img_type == 3:  # 32-bit RGBA
        raw = data[img_off:img_off + w * h * 4]
        return Image.frombytes("RGBA", (w, h), raw)

    return None


# ============================================================================
# 2. MSA (`select.msa`) String Table Loader & Repacker
# ============================================================================

class MsaArchive:
    """
    Parses and repacks `select.msa` (MSAD archive containing SHT key hash table in Chunk 0
    and 8 glyph-indexed localized string pools in Chunks 1..8).
    """

    def __init__(self, path: str = None):
        self.source_path = path
        self.chunks: List[bytearray] = []
        self.key_to_loc: Dict[str, Tuple[int, int, str]] = {}  # key -> (chunk_idx, str_idx, text)
        self.pool_strings: Dict[int, List[str]] = {}           # chunk_idx (1..8) -> list of strings
        self.pool_glyphs: Dict[int, List[int]] = {}            # chunk_idx (1..8) -> list of unicode codepoints
        self.pool_prefix: Dict[int, bytes] = {}                # chunk_idx (1..8) -> header + glyph_table + width_table
        self.chunk_count = 0
        if path and os.path.exists(path):
            self.load(path)

    def load(self, path: str):
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] != MSAD_MAGIC:
            raise ValueError(f"Not an MSAD archive: {path}")
        self.source_path = path
        self.chunk_count = struct.unpack_from("<I", data, 4)[0]
        self.chunks = []
        for i in range(self.chunk_count):
            off, sz = struct.unpack_from("<II", data, 8 + i * 8)
            self.chunks.append(bytearray(data[off:off + sz]))

        # Parse Chunks 1..8 glyph-indexed string pools
        self.pool_strings = {}
        self.pool_glyphs = {}
        self.pool_prefix = {}
        for c_idx in range(1, min(9, len(self.chunks))):
            ch = self.chunks[c_idx]
            if len(ch) < 24:
                continue
            str_cnt = struct.unpack_from("<I", ch, 0)[0]
            glyph_cnt = struct.unpack_from("<H", ch, 4)[0]
            str_tab_off = (20 + glyph_cnt * 16 + str_cnt * 2) & ~3
            if str_tab_off + str_cnt * 4 > len(ch):
                continue

            g_map = [struct.unpack_from("<H", ch, 24 + g * 16)[0] for g in range(glyph_cnt)]
            self.pool_glyphs[c_idx] = g_map
            self.pool_prefix[c_idx] = bytes(ch[:str_tab_off])

            lens = struct.unpack_from(f"<{str_cnt}H", ch, 16 + glyph_cnt * 16)
            offs = struct.unpack_from(f"<{str_cnt}I", ch, str_tab_off)
            data_base = str_tab_off + str_cnt * 4
            total_glyph_slots = (len(ch) - data_base) // 2

            strs = []
            for s_i in range(str_cnt):
                start_g = offs[s_i]
                end_g = start_g + lens[s_i]
                chars = []
                for g_pos in range(start_g, min(end_g, total_glyph_slots)):
                    gi = struct.unpack_from("<H", ch, data_base + g_pos * 2)[0]
                    if gi == 0xFFFF or gi >= len(g_map):
                        break
                    chars.append(chr(g_map[gi]))
                strs.append("".join(chars).rstrip())
            self.pool_strings[c_idx] = strs

        # Parse Chunk 0 (`SHT\x00` hash table)
        self.key_to_loc = {}
        if self.chunks and self.chunks[0][:4] == b"SHT\x00":
            c0 = self.chunks[0]
            num_buckets, max_probe, tab_off = struct.unpack_from("<3I", c0, 4)
            total_slots = num_buckets * max_probe
            str_base = tab_off + total_slots * 8
            for slot in range(total_slots):
                s_idx, val = struct.unpack_from("<II", c0, tab_off + slot * 8)
                if s_idx == 0xFFFFFFFF or val == 0xFFFFFFFF:
                    continue
                k_off = str_base + s_idx * 16
                if k_off >= len(c0):
                    continue
                key = bytes(c0[k_off:k_off + 16]).split(b"\x00")[0].decode("latin1", errors="ignore")
                font_pool = ((val >> 16) & 0xFFFF) + 1
                str_idx = val & 0xFFFF
                pool = self.pool_strings.get(font_pool, [])
                txt = pool[str_idx] if 0 <= str_idx < len(pool) else ""
                self.key_to_loc[key] = (font_pool, str_idx, txt)

    def get(self, key: str, default: str = "") -> str:
        loc = self.key_to_loc.get(key)
        return loc[2] if loc else default

    def set_string(self, key: str, new_text: str):
        if key not in self.key_to_loc:
            return False
        c_idx, s_idx, _ = self.key_to_loc[key]
        self.key_to_loc[key] = (c_idx, s_idx, new_text)
        if c_idx in self.pool_strings and 0 <= s_idx < len(self.pool_strings[c_idx]):
            self.pool_strings[c_idx][s_idx] = new_text
        return True

    def _rebuild_string_pool_chunk(self, c_idx: int):
        if c_idx not in self.pool_prefix or c_idx not in self.pool_strings:
            return
        prefix = self.pool_prefix[c_idx]
        g_map = self.pool_glyphs[c_idx]
        strs = self.pool_strings[c_idx]
        char_to_gi = {chr(cp): gi for gi, cp in enumerate(g_map)}
        space_gi = char_to_gi.get(" ", 0)

        offs = []
        glyph_stream = bytearray()
        cur_g = 0
        for s in strs:
            offs.append(cur_g)
            for ch_c in s:
                gi = char_to_gi.get(ch_c, char_to_gi.get(ch_c.upper(), space_gi))
                glyph_stream.extend(struct.pack("<H", gi))
                cur_g += 1

        out = bytearray(prefix)
        for o in offs:
            out.extend(struct.pack("<I", o))
        out.extend(glyph_stream)
        while len(out) % 16 != 0:
            out.append(0)
        self.chunks[c_idx] = out

    def save(self, out_path: str = None):
        target = out_path or self.source_path
        if not target:
            raise ValueError("No output path specified for MsaArchive.save()")
        for c_idx in range(1, min(9, len(self.chunks))):
            self._rebuild_string_pool_chunk(c_idx)

        header_end = 8 + self.chunk_count * 8
        cur_off = (header_end + 15) & ~15
        table = bytearray(self.chunk_count * 8)
        payloads = []
        for i, ch in enumerate(self.chunks):
            cur_off = (cur_off + 15) & ~15
            struct.pack_into("<II", table, i * 8, cur_off, len(ch))
            payloads.append((cur_off, ch))
            cur_off += len(ch)

        out = bytearray(cur_off)
        out[0:4] = MSAD_MAGIC
        struct.pack_into("<I", out, 4, self.chunk_count)
        out[8:8 + len(table)] = table
        for off, ch in payloads:
            out[off:off + len(ch)] = ch

        with open(target, "wb") as f:
            f.write(out)


# ============================================================================
# 3. SWG / SWAD Scene Graph Document & In-Game Renderer
# ============================================================================

def resolve_op_mat_idx(swg: bytes, off: int) -> int:
    op = struct.unpack_from("<i", swg, off)[0]
    if op in (0, 1):
        return resolve_op_mat_idx(swg, struct.unpack_from("<i", swg, off + 8)[0])
    elif op == 2:
        return struct.unpack_from("<i", swg, off + 12)[0]
    elif op == 3:
        return struct.unpack_from("<i", swg, off + 8)[0]
    elif op in (4, 5):
        return struct.unpack_from("<i", swg, off + 16)[0]
    return -1


def resolve_op_col_idx(swg: bytes, off: int) -> int:
    """
    Matches ACZ.exe SWG op-table color-index resolvers:
      - op 0 (`0x1401fb0f0`): recurses via pointer at `off + 12`
      - op 2 (`0x1401fb130`): recurses via pointer at `off + 8`
      - op 1, 3: direct color index at `off + 12`
      - op 4: no color transform (-1)
      - op 5: direct color index at `off + 20`
    """
    op = struct.unpack_from("<i", swg, off)[0]
    if op == 0:
        return resolve_op_col_idx(swg, struct.unpack_from("<i", swg, off + 12)[0])
    elif op == 2:
        return resolve_op_col_idx(swg, struct.unpack_from("<i", swg, off + 8)[0])
    elif op in (1, 3):
        return struct.unpack_from("<i", swg, off + 12)[0]
    elif op == 4:
        return -1
    elif op == 5:
        return struct.unpack_from("<i", swg, off + 20)[0]
    return -1


def composite_sprite_onto_canvas(
    canvas_img: Image.Image,
    sprite: Image.Image,
    px: int,
    py: int,
    world_alpha: float,
    blend_flag: int
):
    """
    Composites `sprite` onto `canvas_img` at `(px, py)`:
      - `blend_flag == 0`: Standard Alpha Blending (`alpha_composite`)
      - `blend_flag == 1`: PS2 GS Additive Blending (`dst.rgb = min(255, dst.rgb + src.rgb * src_a * world_alpha)`)
    """
    cw, ch = canvas_img.size
    sw, sh = sprite.size

    # Clip destination rectangle to canvas bounds
    x0 = max(0, px)
    y0 = max(0, py)
    x1 = min(cw, px + sw)
    y1 = min(ch, py + sh)
    if x1 <= x0 or y1 <= y0:
        return

    sx0 = x0 - px
    sy0 = y0 - py
    sx1 = sx0 + (x1 - x0)
    sy1 = sy0 + (y1 - y0)
    sub_sprite = sprite.crop((sx0, sy0, sx1, sy1)) if (sx0 != 0 or sy0 != 0 or sx1 != sw or sy1 != sh) else sprite

    if blend_flag == 1:
        # PS2 GS Additive Blending: dst.rgb += src.rgb * (src.a / 255.0) * world_alpha
        r_ch, g_ch, b_ch, a_ch = sub_sprite.split()
        # Scale factor per pixel is (a / 255.0) * world_alpha
        a_bytes = a_ch.tobytes()
        r_bytes = r_ch.tobytes()
        g_bytes = g_ch.tobytes()
        b_bytes = b_ch.tobytes()
        w_scale = float(world_alpha) / 255.0
        # Fast LUT for (val * alpha_scaled)
        add_r = bytes(min(255, int(rv * av * w_scale)) for rv, av in zip(r_bytes, a_bytes))
        add_g = bytes(min(255, int(gv * av * w_scale)) for gv, av in zip(g_bytes, a_bytes))
        add_b = bytes(min(255, int(bv * av * w_scale)) for bv, av in zip(b_bytes, a_bytes))
        sz = (x1 - x0, y1 - y0)
        add_rgb = Image.merge("RGB", (
            Image.frombytes("L", sz, add_r),
            Image.frombytes("L", sz, add_g),
            Image.frombytes("L", sz, add_b),
        ))
        dst_crop = canvas_img.crop((x0, y0, x1, y1)).convert("RGB")
        blended_rgb = ImageChops.add(dst_crop, add_rgb)
        blended_rgba = blended_rgb.convert("RGBA")
        canvas_img.paste(blended_rgba, (x0, y0))
    else:
        if world_alpha < 0.995:
            r_ch, g_ch, b_ch, a_ch = sub_sprite.split()
            a_ch = a_ch.point(lambda p: int(p * world_alpha))
            sub_sprite = Image.merge("RGBA", (r_ch, g_ch, b_ch, a_ch))
        canvas_img.alpha_composite(sub_sprite, dest=(x0, y0))


class SwgDocument:
    """Parses, renders, and edits an SWG layout buffer + embedded TIM2 textures from a .swa archive."""

    def __init__(self, data: bytes, source_path: str = "", swa_chunks: list = None):
        if data[:4] != SWG_MAGIC:
            raise ValueError("Buffer does not start with 'SWG\\x00' magic.")
        self.swg = bytearray(data)
        self.source_path = source_path
        self.swa_chunks = swa_chunks or []

        self.menu_name = bytes(self.swg[8:0x48]).split(b"\x00")[0].decode("latin1", errors="ignore")
        self.canvas_w, self.canvas_h = struct.unpack_from("<HH", self.swg, 0x50)
        self.t0_off, self.t0_cnt     = struct.unpack_from("<2I", self.swg, 0x54)
        self.t1_off, self.t1_cnt     = struct.unpack_from("<2I", self.swg, 0x5C)
        self.t2_off, self.t2_cnt     = struct.unpack_from("<2I", self.swg, 0x64)
        self.t5_off, self.t5_cnt     = struct.unpack_from("<2I", self.swg, 0x7C)

        self.labels_pool  = self._load_sym_pool(0xA0)
        self.targets_pool = self._load_sym_pool(0xB8)
        self.strings_pool = self._load_sym_pool(0xD8)

        # Parse Table 2 (12 bytes per texture: tex_id, wh_packed, blend_flag)
        self.tex_blend_flags: Dict[int, int] = {}
        for i in range(self.t2_cnt):
            if self.t2_off + (i + 1) * 12 <= len(self.swg):
                tid, _wh, bflag = struct.unpack_from("<3I", self.swg, self.t2_off + i * 12)
                self.tex_blend_flags[tid] = bflag

        # Decode embedded TIM2 textures (Chunk 1..N -> tex_id 0..N-1)
        self.textures: Dict[int, Image.Image] = {}
        for i in range(1, len(self.swa_chunks)):
            im = decode_tim2_to_pil(self.swa_chunks[i])
            if im is not None:
                self.textures[i - 1] = im
        self._load_external_aspect_textures()

        self.dict_entries = self._parse_dict()

        # Identify Outer Root MovieClip (last Type 5 entry) and Main Timeline MovieClip
        # Score by: (has >= 3 children on peak frame, label count, peak child count, frame count)
        # so 180-frame 1-child scanline overlays never beat the actual interactive UI timeline!
        self.outer_root_id = 0
        self.main_mc_id = 0
        best_score = (-1, -1, -1, -1)
        for did, ent in self.dict_entries.items():
            if ent["type"] == 5:
                self.outer_root_id = did
                fcount = ent["fcount"]
                lcount = len(ent.get("labels", []))
                max_ops = max((len(f) for f in ent.get("frames", [])), default=0)
                score = (1 if max_ops >= 3 else 0, lcount, max_ops, fcount)
                if score > best_score:
                    best_score = score
                    self.main_mc_id = did

        self.set_active_movieclip(self.main_mc_id)

    def _load_external_aspect_textures(self):
        if not self.source_path:
            return
        folder = os.path.dirname(os.path.abspath(self.source_path))
        if not os.path.isdir(folder):
            return
        suf = "_21x9.png" if self.canvas_w >= 800 else ("_16x9.png" if self.canvas_w > 512 else ".png")
        import glob as _glob
        for tid in list(self.textures.keys()):
            if suf != ".png":
                matches = sorted(_glob.glob(os.path.join(folder, f"entry_{tid:03d}_*{suf}")))
                if matches:
                    try:
                        self.textures[tid] = Image.open(matches[0]).convert("RGBA")
                        continue
                    except Exception:
                        pass
            base_matches = [
                p for p in sorted(_glob.glob(os.path.join(folder, f"entry_{tid:03d}_*.png")))
                if "_16x9.png" not in p and "_21x9.png" not in p
            ]
            if base_matches:
                try:
                    self.textures[tid] = Image.open(base_matches[0]).convert("RGBA")
                except Exception:
                    pass

    def set_active_movieclip(self, mc_id: int):
        if mc_id in self.dict_entries and self.dict_entries[mc_id]["type"] == 5:
            self.main_mc_id = mc_id
            self.root_id = mc_id
            self.root_mc = self.dict_entries[mc_id]

    def get_movieclip_list(self) -> List[Tuple[int, str]]:
        """Returns a list of `(did, description)` for all Type 5 MovieClips in the document."""
        res = []
        for did, ent in sorted(self.dict_entries.items()):
            if ent["type"] == 5:
                fc = ent["fcount"]
                lc = len(ent.get("labels", []))
                max_c = max((len(f) for f in ent.get("frames", [])), default=0)
                tag = " [OuterRoot]" if did == self.outer_root_id else (" [MainUI]" if did == self.main_mc_id else "")
                lbl_preview = ""
                if ent.get("labels"):
                    lbl_preview = " (" + ", ".join(n for n, _ in ent["labels"][:3]) + ("..." if lc > 3 else "") + ")"
                res.append((did, f"Dict[{did}]: {fc}f, {max_c}ch, {lc}lbl{lbl_preview}{tag}"))
        return res

    @classmethod
    def load_file(cls, path: str):
        with open(path, "rb") as f:
            raw = f.read()
        if raw[:4] == SWAD_MAGIC:
            num_items = struct.unpack_from("<I", raw, 4)[0]
            chunks = []
            for i in range(num_items):
                off, sz = struct.unpack_from("<II", raw, 8 + i * 8)
                chunks.append(raw[off:off + sz])
            return cls(chunks[0], source_path=path, swa_chunks=chunks)
        elif raw[:4] == SWG_MAGIC:
            return cls(raw, source_path=path)
        else:
            raise ValueError(f"Unsupported file format: {path}")

    def _load_sym_pool(self, header_off: int) -> List[str]:
        if header_off + 8 > len(self.swg):
            return []
        cnt, toff = struct.unpack_from("<2I", self.swg, header_off)
        if cnt == 0 or toff + cnt * 4 > len(self.swg):
            return []
        offs = struct.unpack_from(f"<{cnt}I", self.swg, toff)
        res = []
        for o in offs:
            if o < len(self.swg):
                end = self.swg.find(b"\x00", o)
                res.append(self.swg[o:end].decode("latin1", "ignore"))
            else:
                res.append("")
        return res

    def _parse_dict(self):
        entries = {}
        for did in range(self.t5_cnt):
            dtype, doff = struct.unpack_from("<ii", self.swg, self.t5_off + did * 8)
            info = {"id": did, "type": dtype, "offset": doff}
            if dtype == 0:
                cnt = struct.unpack_from("<i", self.swg, doff)[0]
                recs = []
                roff = doff + 4
                for r in range(cnt):
                    rtype, noff, midx, tex = struct.unpack_from("<4i", self.swg, roff)
                    rgba = struct.unpack_from("<4B", self.swg, roff + 16)
                    x0, y0, w, h = struct.unpack_from("<4f", self.swg, roff + 20)
                    recs.append({
                        "rec_index": r,
                        "roff": roff,
                        "tex_id": tex,
                        "mat_idx": midx,
                        "rgba": rgba,
                        "x0": x0, "y0": y0, "w": w, "h": h
                    })
                    roff = noff
                info["records"] = recs
            elif dtype == 3:
                u = struct.unpack_from("<8I", self.swg, doff)
                font_sz = u[2] / 65536.0
                box_w   = u[5] / 65536.0
                rgba    = struct.unpack_from("<4B", self.swg, doff + 24)
                info["font_size"] = font_sz if font_sz > 0 else 14.0
                info["box_w"]     = box_w if box_w > 0 else 160.0
                info["rgba"]      = rgba
                info["rect"]      = (0.0, 0.0, max(120.0, box_w * 3.5), max(18.0, font_sz + 4.0))
            elif dtype == 5:
                mc_id, lbl_off, lbl_cnt, frm_off, frm_cnt = struct.unpack_from("<5i", self.swg, doff)
                labels = []
                for li in range(lbl_cnt):
                    l_idx, f_num = struct.unpack_from("<2i", self.swg, lbl_off + li * 8)
                    lname = self.labels_pool[l_idx] if 0 <= l_idx < len(self.labels_pool) else f"lbl_{l_idx}"
                    labels.append((lname, f_num))
                frames = []
                frames_as = []
                for fi in range(frm_cnt):
                    boff, opcnt = struct.unpack_from("<ii", self.swg, frm_off + fi * 8)
                    placements = []
                    as_list = []
                    p = boff
                    slot = 0
                    in_mask_def = False
                    for _ in range(opcnt):
                        op = struct.unpack_from("<i", self.swg, p)[0]
                        if op == 10:
                            in_mask_def = True
                            p += 4
                            continue
                        elif op == 11:
                            in_mask_def = False
                            p += 4
                            continue
                        if op in (0, 1, 2, 3):
                            _, poff, _, _ = struct.unpack_from("<4i", self.swg, p)
                            _, depth, child_d, sym_idx = struct.unpack_from("<4i", self.swg, poff)
                            midx = resolve_op_mat_idx(self.swg, p)
                            if midx < 0:
                                midx = resolve_op_mat_idx(self.swg, poff)
                            cidx = resolve_op_col_idx(self.swg, p)
                            sname = self.targets_pool[sym_idx] if 0 <= sym_idx < len(self.targets_pool) else ""
                            placements.append({
                                "slot": slot, "op": op, "child_id": child_d,
                                "depth": depth, "sym_idx": sym_idx, "sym_name": sname,
                                "mat_idx": midx, "col_idx": cidx, "is_mask": in_mask_def
                            })
                            slot += 1
                            p += 16
                        elif op in (4, 5):
                            _, depth, child_d, sym_idx = struct.unpack_from("<4i", self.swg, p)
                            midx = resolve_op_mat_idx(self.swg, p)
                            cidx = resolve_op_col_idx(self.swg, p)
                            sname = self.targets_pool[sym_idx] if 0 <= sym_idx < len(self.targets_pool) else ""
                            placements.append({
                                "slot": slot, "op": op, "child_id": child_d,
                                "depth": depth, "sym_idx": sym_idx, "sym_name": sname,
                                "mat_idx": midx, "col_idx": cidx, "is_mask": in_mask_def
                            })
                            slot += 1
                            p += 20 if op == 4 else 24
                        elif op == 6:
                            as_off = struct.unpack_from("<i", self.swg, p + 4)[0]
                            as_list.append(as_off)
                            p += 8
                        elif op in (8, 9):
                            p += 8
                        elif op == 7:
                            p += 16
                        else:
                            p += 4
                    # Hold Frame support: if this frame has no new placement ops (opcnt == 0 or ActionScript-only),
                    # inherit the active display list from the previous frame.
                    if not placements and frames:
                        placements = [dict(x) for x in frames[-1]]
                    frames.append(placements)
                    frames_as.append(as_list)
                info["fcount"]    = frm_cnt
                info["labels"]    = labels
                info["frames"]    = frames
                info["frames_as"] = frames_as
            entries[did] = info
        return entries

    def get_matrix(self, mat_idx: int) -> Tuple[float, float, float, float, float, float]:
        if mat_idx < 0 or mat_idx >= self.t0_cnt:
            return (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
        m = struct.unpack_from("<16f", self.swg, self.t0_off + mat_idx * 64)
        return (m[0], m[1], m[4], m[5], m[12], m[13])

    def set_matrix(self, mat_idx: int, m00: float, m01: float, m10: float, m11: float, tx: float, ty: float):
        if 0 <= mat_idx < self.t0_cnt:
            pos = self.t0_off + mat_idx * 64
            struct.pack_into("<ff", self.swg, pos + 0, m00, m01)
            struct.pack_into("<ff", self.swg, pos + 16, m10, m11)
            struct.pack_into("<ff", self.swg, pos + 48, tx, ty)

    def get_quad_wh(self, did: int, rec_idx: int = 0) -> Optional[Tuple[float, float]]:
        ent = self.dict_entries.get(did)
        if ent and ent["type"] == 0 and rec_idx < len(ent["records"]):
            r = ent["records"][rec_idx]
            return (r["w"], r["h"])
        return None

    def set_quad_wh(self, did: int, w: float, h: float, rec_idx: int = 0):
        ent = self.dict_entries.get(did)
        if ent and ent["type"] == 0 and rec_idx < len(ent["records"]):
            r = ent["records"][rec_idx]
            r["w"], r["h"] = float(w), float(h)
            struct.pack_into("<ff", self.swg, r["roff"] + 28, r["w"], r["h"])

    def get_color_alpha(self, col_idx: int) -> float:
        if col_idx < 0 or col_idx >= self.t1_cnt:
            return 1.0
        a_mult = struct.unpack_from("<f", self.swg, self.t1_off + col_idx * 32 + 12)[0]
        return max(0.0, min(1.0, a_mult))

    def set_canvas_width(self, width: int):
        self.canvas_w = int(width)
        struct.pack_into("<H", self.swg, 0x50, self.canvas_w)

    def get_child_bbox(self, did: int, frame_idx: int = 0, depth_limit: int = 3) -> Tuple[float, float, float, float]:
        ent = self.dict_entries.get(did)
        if not ent or depth_limit < 0:
            return (0.0, 0.0, 48.0, 20.0)
        dtype = ent["type"]
        if dtype == 0 and ent.get("records"):
            xs, ys = [], []
            for r in ent["records"]:
                m00, m01, m10, m11, tx, ty = self.get_matrix(r["mat_idx"])
                w, h = r["w"], r["h"]
                for px, py in ((0.0, 0.0), (w, 0.0), (w, h), (0.0, h)):
                    xs.append(px * m00 + py * m10 + tx)
                    ys.append(px * m01 + py * m11 + ty)
            return (min(xs), min(ys), max(xs), max(ys))
        elif dtype == 3:
            return ent.get("rect", (0.0, 0.0, 140.0, 20.0))
        elif dtype == 5:
            frames = ent.get("frames", [])
            if not frames:
                return (0.0, 0.0, 64.0, 24.0)
            f_use = min(frame_idx, len(frames) - 1) if len(frames) > 10 else 0
            if not frames[f_use]:
                return (0.0, 0.0, 64.0, 24.0)
            xs, ys = [], []
            for c in frames[f_use]:
                if self.get_color_alpha(c.get("col_idx", -1)) <= 0.01:
                    continue
                bx0, by0, bx1, by1 = self.get_child_bbox(c["child_id"], frame_idx, depth_limit - 1)
                m00, m01, m10, m11, tx, ty = self.get_matrix(c["mat_idx"])
                for px, py in ((bx0, by0), (bx1, by0), (bx1, by1), (bx0, by1)):
                    xs.append(px * m00 + py * m10 + tx)
                    ys.append(px * m01 + py * m11 + ty)
            if xs and ys:
                return (min(xs), min(ys), max(xs), max(ys))
        return (0.0, 0.0, 48.0, 20.0)

    def resolve_active_msa_texts(self, frame_idx: int, msa: Optional[MsaArchive]) -> Dict[str, any]:
        """Resolves active W (adress), B (but), M (msg), and H (buttons) text strings at frame_idx."""
        mc = self.dict_entries.get(self.main_mc_id, {})
        frames_as = mc.get("frames_as", [])
        all_offs = sorted(set(off for fl in frames_as for off in fl))

        w_key, b_key, m_key = None, None, None
        h_keys = []

        for s in self.strings_pool:
            if len(s) >= 6 and "_" in s:
                if s.startswith("W") and not w_key:
                    w_key = s
                elif s.startswith("B") and not b_key:
                    b_key = s
                elif s.startswith("M") and not m_key:
                    m_key = s
                elif s.startswith("H") and not s.endswith("N") and len(h_keys) < 6:
                    h_keys.append(s)

        slot_map = {}
        tf_map = {}

        def _scan_as_words(u, base_off, cur_h):
            nonlocal w_key, b_key, m_key, tf_map
            for k in range(len(u) - 1):
                if u[k] == 0 and 0 <= u[k + 1] < len(self.strings_pool):
                    s = self.strings_pool[u[k + 1]]
                    if len(s) >= 6 and "_" in s:
                        if s.startswith("W") and len(s) >= 10:
                            w_key = s
                        elif s.startswith("B") and len(s) >= 10:
                            b_key = s
                        elif s.startswith("M") and len(s) >= 10:
                            m_key = s
                        elif s.startswith("H") and len(s) >= 10 and not s.endswith("N") and s not in cur_h:
                            cur_h.append(s)
                if k + 6 < len(u) and u[k] == 0 and u[k+2] == 7 and u[k+4] == 7 and u[k+5] == 5:
                    s_sym, sl_id = u[k+1], u[k+3]
                    if 0 <= s_sym < len(self.strings_pool):
                        slot_map[f"@{sl_id}"] = self.strings_pool[s_sym]
                elif k + 9 < len(u) and u[k] == 0 and u[k+2] == 73 and u[k+3] == 46 and u[k+4] == 16 and u[k+5] == 0 and u[k+7] == 0 and u[k+9] == 74:
                    tf_s, m_s = u[k+1], u[k+8]
                    if 0 <= tf_s < len(self.strings_pool) and 0 <= m_s < len(self.strings_pool):
                        tf_n, sl_n = self.strings_pool[tf_s], self.strings_pool[m_s]
                        if tf_n in ("t_A_1", "t_B_1"):
                            tf_map = {k: v for k, v in tf_map.items() if not k.startswith(tf_n[:4])}
                        if sl_n in slot_map:
                            tf_map[tf_n] = slot_map[sl_n]
                elif u[k] == 47:
                    target = base_off + k * 4 + 8 + u[k+1]
                    if (0xB5D5C <= target < 0xB802C or 0xDA73C <= target < 0xDAFE4) and target + 280 <= len(self.swg):
                        c_span = min(600, len(self.swg) - target) & ~3
                        cu = struct.unpack_from(f"<{c_span // 4}i", self.swg, target)
                        for ck in range(len(cu) - 9):
                            if cu[ck] == 0 and cu[ck+2] == 7 and cu[ck+4] == 7 and cu[ck+5] == 5:
                                s_sym, sl_id = cu[ck+1], cu[ck+3]
                                if 0 <= s_sym < len(self.strings_pool):
                                    slot_map[f"@{sl_id}"] = self.strings_pool[s_sym]
                            elif cu[ck] == 0 and cu[ck+2] == 73 and cu[ck+3] == 46 and cu[ck+4] == 16 and cu[ck+5] == 0 and cu[ck+7] == 0 and cu[ck+9] == 74:
                                tf_s, m_s = cu[ck+1], cu[ck+8]
                                if 0 <= tf_s < len(self.strings_pool) and 0 <= m_s < len(self.strings_pool):
                                    tf_n, sl_n = self.strings_pool[tf_s], self.strings_pool[m_s]
                                    if sl_n in slot_map:
                                        tf_map[tf_n] = slot_map[sl_n]

        for fi in range(0, min(frame_idx + 1, len(frames_as))):
            for as_off in frames_as[fi]:
                idx = all_offs.index(as_off)
                end_off = all_offs[idx + 1] if idx + 1 < len(all_offs) else self.t5_off
                span = max(0, min(end_off - as_off, 16384))
                if span < 8:
                    continue
                u = struct.unpack_from(f"<{span // 4}i", self.swg, as_off)
                cur_h = []
                tf_frame = {}
                _scan_as_words(u, as_off, cur_h)
                if cur_h:
                    h_keys = cur_h

        def look(k):
            if not k:
                return ""
            txt = msa.get(k, k) if msa else k
            return txt.replace("\\", "  ")

        return {
            "adress": look(w_key),
            "but":    look(b_key),
            "msg":    look(m_key),
            "buttons": [look(hk) for hk in h_keys],
            "tf_texts": {k: look(v) for k, v in tf_map.items()},
            "keys": {"W": w_key, "B": b_key, "M": m_key, "H": h_keys}
        }

    def render_scene_to_pil(
        self,
        frame_idx: int,
        msa: Optional[MsaArchive] = None,
        custom_text_overrides: Optional[Dict[str, str]] = None
    ) -> Tuple[Image.Image, List[Dict]]:
        """
        Renders the complete SWG scene at `frame_idx` onto an RGBA PIL.Image of size `(canvas_w, canvas_h)`,
        returning `(pil_image, interactive_hit_boxes)`.
        """
        cw, ch = self.canvas_w, self.canvas_h
        x_origin = (cw - 512.0) / 2.0
        canvas_img = Image.new("RGBA", (cw, ch), (8, 10, 14, 255))

        texts_info = self.resolve_active_msa_texts(frame_idx, msa)
        main_frames = self.root_mc.get("frames", [])
        if main_frames:
            f_cur = main_frames[min(frame_idx, len(main_frames) - 1)]
            t_list = []
            for c in f_cur:
                sn = c.get("sym_name", "")
                if sn.startswith("target_") and sn not in ("target_cmpB", "target_B") and not sn.endswith("_C"):
                    _, _, _, _, _, c_ty = self.get_matrix(c["mat_idx"])
                    t_list.append((c_ty, sn))
            t_list.sort(key=lambda x: x[0])
            texts_info["active_targets"] = [sn for _, sn in t_list]
        if custom_text_overrides:
            texts_info.update(custom_text_overrides)
        btn_counter = [0]

        def combine_mat(p_mat, c_mat):
            pm00, pm01, pm10, pm11, ptx, pty = p_mat
            cm00, cm01, cm10, cm11, ctx, cty = c_mat
            return (
                pm00 * cm00 + pm10 * cm01,
                pm01 * cm00 + pm11 * cm01,
                pm00 * cm10 + pm10 * cm11,
                pm01 * cm10 + pm11 * cm11,
                pm00 * ctx + pm10 * cty + ptx,
                pm01 * ctx + pm11 * cty + pty
            )

        def draw_node(did: int, world_mat, world_alpha: float, sym_hint: str, depth_rem: int):
            if world_alpha <= 0.01 or depth_rem < 0:
                return
            ent = self.dict_entries.get(did)
            if not ent:
                return
            dtype = ent["type"]

            if dtype == 0:
                for r in ent.get("records", []):
                    rmat = combine_mat(world_mat, self.get_matrix(r["mat_idx"]))
                    m00, m01, m10, m11, tx, ty = rmat
                    w, h = r["w"], r["h"]
                    rw = int(round(abs(w * m00)))
                    rh = int(round(abs(h * m11)))
                    if rw <= 0 or rh <= 0 or rw > 4096 or rh > 4096:
                        continue
                    px = int(round(x_origin + (tx if m00 >= 0 else tx + w * m00)))
                    py = int(round(ty if m11 >= 0 else ty + h * m11))

                    tex_id = r["tex_id"]
                    tex_im = self.textures.get(tex_id)
                    if tex_im is not None:
                        sprite = tex_im.resize((rw, rh), Image.Resampling.BILINEAR) if (tex_im.size != (rw, rh)) else tex_im
                        if m00 < 0:
                            sprite = sprite.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                        if m11 < 0:
                            sprite = sprite.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                        bflag = self.tex_blend_flags.get(tex_id, 0)
                        composite_sprite_onto_canvas(canvas_img, sprite, px, py, world_alpha, bflag)

            elif dtype == 3:
                m00, m01, m10, m11, tx, ty = world_mat
                px = int(round(x_origin + tx))
                py = int(round(ty))
                rgba = ent.get("rgba", (251, 221, 30, 255))
                alpha_int = int(rgba[3] * world_alpha)
                fill_col = (rgba[0], rgba[1], rgba[2], alpha_int)

                SYM_TO_MSA = {
                    "target_campaign":    "H00000_001",
                    "target_vs":          "H00000_002",
                    "target_options":     "H00000_003",
                    "target_newgame":     "H00000_004",
                    "target_spnewgame":   "H00000_005",
                    "target_continue":    "H00000_006",
                    "target_freemission": "H00000_007",
                    "target_acestyle":    "H00000_008",
                    "target_memorycard":  "H00000_009",
                    "target_conSet":      "H00000_010",
                    "target_lanSet":      "H00000_011",
                    "target_disSet":      "H00000_012",
                    "target_souSet":      "H00000_013",
                    "target_scrSet":      "H00000_014",
                    "target_flightData":  "H00000_022",
                    "target_gallery":     "H00000_023",
                    "target_save":        "H00000_024",
                    "target_load":        "H00000_025",
                    "target_qSelect":     "H00000_030",
                    "target_wCommand":    "H00000_031",
                    "target_hud":         "H00000_032",
                    "target_hudScale":    "H00000_033",
                    "target_sPosition":   "H00000_034",
                    "target_brightness":  "H00000_035",
                    "target_sRatio":      "H00000_036",
                }
                label_str = ""
                if rgba[0] <= 140 and rgba[1] <= 140 and rgba[2] <= 140:
                    label_str = ""
                elif sym_hint == "adress":
                    label_str = texts_info.get("adress", "MAIN MENU")
                elif sym_hint == "but":
                    label_str = texts_info.get("but", "OK      CANCEL")
                elif sym_hint == "msg":
                    label_str = texts_info.get("msg", "")
                elif sym_hint == "brtLevel":
                    label_str = "10"
                elif sym_hint in SYM_TO_MSA and msa:
                    label_str = msa.get(SYM_TO_MSA[sym_hint], sym_hint)
                elif sym_hint.startswith("t_A_") and msa:
                    tf_t = texts_info.get("tf_texts", {}).get(sym_hint, "")
                    if tf_t:
                        label_str = tf_t
                    else:
                        idx_a = int(sym_hint.split("_")[-1]) - 1
                        active_targets = texts_info.get("active_targets", [])
                        if 0 <= idx_a < len(active_targets):
                            t_sym = active_targets[idx_a]
                            msa_k = SYM_TO_MSA.get(t_sym)
                            label_str = msa.get(msa_k, t_sym) if msa_k else t_sym
                elif (sym_hint.startswith("t_B_") or sym_hint.startswith("t_C_")) and msa:
                    label_str = texts_info.get("tf_texts", {}).get(sym_hint, "")
                else:
                    btns = texts_info.get("buttons", [])
                    idx_b = btn_counter[0]
                    if idx_b < len(btns):
                        label_str = btns[idx_b]
                    else:
                        label_str = sym_hint or f"TEXT_{did}"
                    btn_counter[0] += 1

                if label_str:
                    draw = ImageDraw.Draw(canvas_img)
                    f_sz = max(10, int(round(ent.get("font_size", 14.0))))
                    try:
                        font = ImageFont.truetype("DejaVuSans-Bold.ttf", f_sz)
                    except Exception:
                        font = ImageFont.load_default()
                    draw.text((px, py), label_str, fill=fill_col, font=font)

            elif dtype == 5:
                frames = ent.get("frames", [])
                if not frames:
                    return
                if did == self.main_mc_id:
                    f_use = min(frame_idx, len(frames) - 1)
                elif did == self.outer_root_id:
                    f_use = min(frame_idx, len(frames) - 1)
                elif len(frames) >= 40:
                    f_use = frame_idx % len(frames)
                else:
                    f_use = 0

                for c in frames[f_use]:
                    if c.get("is_mask"):
                        continue
                    c_alpha = world_alpha * self.get_color_alpha(c.get("col_idx", -1))
                    if c_alpha <= 0.01:
                        continue
                    c_mat = combine_mat(world_mat, self.get_matrix(c["mat_idx"]))
                    c_sym = c.get("sym_name") or sym_hint
                    draw_node(c["child_id"], c_mat, c_alpha, c_sym, depth_rem - 1)

        ident = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
        # Check if main_mc_id is referenced inside outer_root_id
        outer_mc = self.dict_entries.get(self.outer_root_id, {})
        outer_refs_main = any(
            c["child_id"] == self.main_mc_id
            for f in outer_mc.get("frames", [])
            for c in f
        )
        if self.outer_root_id != self.main_mc_id and outer_refs_main:
            draw_node(self.outer_root_id, ident, 1.0, "", 6)
        else:
            draw_node(self.main_mc_id, ident, 1.0, "", 6)

        # Build interactive hit-boxes for active children of main_mc_id
        hit_boxes = []
        main_frames = self.root_mc.get("frames", [])
        if main_frames:
            f_main = min(frame_idx, len(main_frames) - 1)
            for c in main_frames[f_main]:
                if self.get_color_alpha(c.get("col_idx", -1)) <= 0.01:
                    continue
                did = c["child_id"]
                m00, m01, m10, m11, tx, ty = self.get_matrix(c["mat_idx"])
                bx0, by0, bx1, by1 = self.get_child_bbox(did, frame_idx)
                x0 = x_origin + min(bx0 * m00 + tx, bx1 * m00 + tx)
                x1 = x_origin + max(bx0 * m00 + tx, bx1 * m00 + tx)
                y0 = min(by0 * m11 + ty, by1 * m11 + ty)
                y1 = max(by0 * m11 + ty, by1 * m11 + ty)
                hit_boxes.append({
                    "placement": c,
                    "bbox_canvas": (x0, y0, x1, y1),
                    "local_bbox": (bx0, by0, bx1, by1)
                })

        return canvas_img, hit_boxes

    def save_to(self, out_path: str):
        if out_path.lower().endswith(".swa") and self.swa_chunks:
            from ACZ_SWAD_Converter import repack_swad_from_buffers
            repack_swad_from_buffers([bytes(self.swg)] + list(self.swa_chunks[1:]), out_path)
        else:
            with open(out_path, "wb") as f:
                f.write(self.swg)


# Simple alias to avoid typo in function name
composite_sprite_onto_canvas = composite_sprite_onto_canvas if "composite_sprite_onto_canvas" in globals() else None


# ============================================================================
# 4. Interactive Tkinter WYSIWYG GUI Editor (with Zoom/Pan, MC Selector, 8-Handle Resize & MSA Tab)
# ============================================================================

def launch_tkinter_editor(initial_path: str = None):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    from PIL import ImageTk

    class SwgVisualEditorApp(tk.Tk):
        HANDLE_RADIUS = 5
        ZOOM_LEVELS = [0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0]

        def __init__(self, init_p: str = None):
            super().__init__()
            self.title("ACZ SWG/SWAD Widescreen Visual Editor v2.1 — Ace Combat Zero Enhancer")
            self.geometry("1520x880")
            self.configure(bg="#181920")

            self.doc: Optional[SwgDocument] = None
            self.msa: Optional[MsaArchive] = None
            self.current_frame = 15
            self.selected_slot: Optional[int] = None
            self.hit_boxes: List[Dict] = []
            self.drag_mode: Optional[str] = None  # 'move' or handle 'N','S','E','W','NW','NE','SW','SE'
            self.drag_state = None
            self.pan_start: Optional[Tuple[int, int, float, float]] = None
            self.zoom: float = 1.0
            self.pan_x: float = 0.0
            self.pan_y: float = 0.0
            self.tk_img_ref = None

            self._auto_load_default_msa()
            self._build_ui()
            if init_p and os.path.exists(init_p):
                self.load_path(init_p)

        def _auto_load_default_msa(self):
            candidates = [
                os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mods", "textures", "menus", "select.msa"),
                "mods/textures/menus/select.msa",
                "select.msa",
                "/tmp/1us_dir/data/select/select.msa"
            ]
            for c in candidates:
                if os.path.exists(c):
                    try:
                        self.msa = MsaArchive(c)
                        break
                    except Exception:
                        pass

        def _build_ui(self):
            # Top Toolbar Row 1
            toolbar = tk.Frame(self, bg="#252732", pady=5, padx=8)
            toolbar.pack(side=tk.TOP, fill=tk.X)

            btn_cfg = {"bg": "#3a3e52", "fg": "#ffffff", "relief": tk.FLAT, "padx": 9, "pady": 4}
            tk.Button(toolbar, text="Open .swa / .swg", command=self.on_open, **btn_cfg).pack(side=tk.LEFT, padx=2)
            tk.Button(toolbar, text="Save .swa / .swg", command=self.on_save, **btn_cfg).pack(side=tk.LEFT, padx=2)
            tk.Button(toolbar, text="Load select.msa", command=self.on_open_msa, **btn_cfg).pack(side=tk.LEFT, padx=2)

            tk.Label(toolbar, text=" Viewport:", bg="#252732", fg="#b8bcc8").pack(side=tk.LEFT, padx=3)
            self.aspect_var = tk.StringVar(value="16:9 (682x448)")
            aspect_cb = ttk.Combobox(
                toolbar, textvariable=self.aspect_var, state="readonly", width=14,
                values=["4:3 (512x448)", "16:9 (682x448)", "21:9 (896x448)"]
            )
            aspect_cb.pack(side=tk.LEFT, padx=2)
            aspect_cb.bind("<<ComboboxSelected>>", self.on_aspect_changed)

            tk.Button(toolbar, text="Auto 16:9", command=lambda: self.on_auto_widescreen("16x9"),
                      bg="#2b5b84", fg="#ffffff", relief=tk.FLAT, padx=8, pady=4).pack(side=tk.LEFT, padx=3)
            tk.Button(toolbar, text="Auto 21:9", command=lambda: self.on_auto_widescreen("21x9"),
                      bg="#2b5b84", fg="#ffffff", relief=tk.FLAT, padx=8, pady=4).pack(side=tk.LEFT, padx=2)

            # MovieClip Selector
            tk.Label(toolbar, text=" MC:", bg="#252732", fg="#b8bcc8").pack(side=tk.LEFT, padx=3)
            self.mc_var = tk.StringVar()
            self.mc_cb = ttk.Combobox(toolbar, textvariable=self.mc_var, state="readonly", width=26)
            self.mc_cb.pack(side=tk.LEFT, padx=2)
            self.mc_cb.bind("<<ComboboxSelected>>", self.on_mc_selected)

            # Named Frame Label Selector
            tk.Label(toolbar, text=" Label:", bg="#252732", fg="#b8bcc8").pack(side=tk.LEFT, padx=3)
            self.label_var = tk.StringVar()
            self.label_cb = ttk.Combobox(toolbar, textvariable=self.label_var, state="readonly", width=18)
            self.label_cb.pack(side=tk.LEFT, padx=2)
            self.label_cb.bind("<<ComboboxSelected>>", self.on_label_selected)

            # Zoom Controls
            tk.Label(toolbar, text=" Zoom:", bg="#252732", fg="#ffd966").pack(side=tk.LEFT, padx=3)
            tk.Button(toolbar, text=" - ", command=self.on_zoom_out, bg="#3a3e52", fg="#ffffff", relief=tk.FLAT, padx=5, pady=2).pack(side=tk.LEFT, padx=1)
            self.zoom_var = tk.StringVar(value="100%")
            self.zoom_cb = ttk.Combobox(
                toolbar, textvariable=self.zoom_var, state="readonly", width=7,
                values=[f"{int(z * 100)}%" for z in self.ZOOM_LEVELS]
            )
            self.zoom_cb.pack(side=tk.LEFT, padx=1)
            self.zoom_cb.bind("<<ComboboxSelected>>", self.on_zoom_cb)
            tk.Button(toolbar, text=" + ", command=self.on_zoom_in, bg="#3a3e52", fg="#ffffff", relief=tk.FLAT, padx=5, pady=2).pack(side=tk.LEFT, padx=1)
            tk.Button(toolbar, text="Reset View", command=self.on_reset_view, bg="#3a3e52", fg="#d0d4e0", relief=tk.FLAT, padx=6, pady=2).pack(side=tk.LEFT, padx=3)

            self.show_tex_var = tk.BooleanVar(value=True)
            tk.Checkbutton(
                toolbar, text="Textures", variable=self.show_tex_var, command=self.redraw_canvas,
                bg="#252732", fg="#ffffff", selectcolor="#181920", activebackground="#252732"
            ).pack(side=tk.LEFT, padx=4)

            self.show_wire_var = tk.BooleanVar(value=True)
            tk.Checkbutton(
                toolbar, text="Boxes", variable=self.show_wire_var, command=self.redraw_canvas,
                bg="#252732", fg="#ffffff", selectcolor="#181920", activebackground="#252732"
            ).pack(side=tk.LEFT, padx=2)

            # Timeline Slider Bar
            slider_bar = tk.Frame(self, bg="#1f2029", pady=4, padx=8)
            slider_bar.pack(side=tk.TOP, fill=tk.X)
            tk.Label(slider_bar, text="Timeline Frame:", bg="#1f2029", fg="#d0d4e0").pack(side=tk.LEFT)
            self.frame_slider = tk.Scale(
                slider_bar, from_=0, to=100, orient=tk.HORIZONTAL,
                bg="#1f2029", fg="#ffffff", highlightthickness=0, troughcolor="#343746",
                command=self.on_frame_slider
            )
            self.frame_slider.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=8)

            # Main Split Pane
            main_pane = tk.PanedWindow(self, orient=tk.HORIZONTAL, bg="#181920", sashwidth=6)
            main_pane.pack(fill=tk.BOTH, expand=True)

            # Center Interactive Canvas
            canvas_frame = tk.Frame(main_pane, bg="#101216")
            self.canvas = tk.Canvas(canvas_frame, bg="#0d0f13", highlightthickness=0)
            self.canvas.pack(fill=tk.BOTH, expand=True)
            self.canvas.bind("<ButtonPress-1>", self.on_canvas_press)
            self.canvas.bind("<B1-Motion>", self.on_canvas_drag)
            self.canvas.bind("<ButtonRelease-1>", self.on_canvas_release)
            # Right-click & Middle-click Pan
            self.canvas.bind("<ButtonPress-2>", self.on_pan_start)
            self.canvas.bind("<B2-Motion>", self.on_pan_drag)
            self.canvas.bind("<ButtonPress-3>", self.on_pan_start)
            self.canvas.bind("<B3-Motion>", self.on_pan_drag)
            # MouseWheel Zoom (Windows/macOS & Linux X11)
            self.canvas.bind("<MouseWheel>", self.on_mousewheel_zoom)
            self.canvas.bind("<Button-4>", lambda e: self._step_zoom(1, e.x, e.y))
            self.canvas.bind("<Button-5>", lambda e: self._step_zoom(-1, e.x, e.y))
            main_pane.add(canvas_frame, width=1000)

            # Right Tabbed Inspector Notebook
            right_notebook = ttk.Notebook(main_pane)
            main_pane.add(right_notebook, width=500)

            # Tab 1: Scene Hierarchy & Matrix/Resize Inspector
            tab_scene = tk.Frame(right_notebook, bg="#21232d", padx=8, pady=8)
            right_notebook.add(tab_scene, text="Scene & Transform")

            tk.Label(tab_scene, text="Active Frame Elements (LMB: Select/Move/Resize | RMB: Pan | Wheel: Zoom):",
                     bg="#21232d", fg="#ffffff", font=("Segoe UI", 9, "bold")).pack(anchor=tk.W)

            cols = ("slot", "did", "name", "mat", "tx", "ty", "sx", "sy")
            self.tree = ttk.Treeview(tab_scene, columns=cols, show="headings", height=14)
            for c, hdr, w in (
                ("slot", "Slot", 38), ("did", "Dict", 48), ("name", "Name / Type", 110),
                ("mat", "Mat#", 50), ("tx", "X (tx)", 55), ("ty", "Y (ty)", 55),
                ("sx", "ScaleX", 52), ("sy", "ScaleY", 52)
            ):
                self.tree.heading(c, text=hdr)
                self.tree.column(c, width=w, anchor=tk.W)
            self.tree.pack(fill=tk.BOTH, expand=True, pady=6)
            self.tree.bind("<<TreeviewSelect>>", self.on_tree_select)

            insp = tk.LabelFrame(
                tab_scene, text="Transform & Edge Resize Inspector (Table 0 Matrix)",
                bg="#21232d", fg="#9ec5fe", padx=8, pady=6
            )
            insp.pack(fill=tk.X, pady=4)

            self.mat_vars = {}
            fields = [
                ("tx (Pos X)", "tx"), ("ty (Pos Y)", "ty"),
                ("m00 (Scale X)", "m00"), ("m11 (Scale Y)", "m11"),
                ("m01 (Skew Y)", "m01"), ("m10 (Skew X)", "m10")
            ]
            for idx, (label_txt, key) in enumerate(fields):
                r, c = divmod(idx, 2)
                tk.Label(insp, text=label_txt, bg="#21232d", fg="#d0d4e0").grid(row=r, column=c * 2, sticky=tk.W, padx=4, pady=2)
                var = tk.StringVar(value="0.0")
                ent = tk.Entry(insp, textvariable=var, width=10, bg="#15161e", fg="#ffffff", insertbackground="#ffffff")
                ent.grid(row=r, column=c * 2 + 1, padx=4, pady=2)
                self.mat_vars[key] = var

            tk.Button(
                insp, text="Apply Matrix Transform", command=self.on_apply_matrix,
                bg="#3b6e52", fg="#ffffff", relief=tk.FLAT, padx=10, pady=4
            ).grid(row=3, column=0, columnspan=4, sticky=tk.EW, pady=5)

            # Tab 2: Text & MSA Localization Editor
            tab_text = tk.Frame(right_notebook, bg="#21232d", padx=8, pady=8)
            right_notebook.add(tab_text, text="Text & MSA Editor")

            search_row = tk.Frame(tab_text, bg="#21232d")
            search_row.pack(fill=tk.X, pady=2)
            tk.Label(search_row, text="Filter MSA Keys:", bg="#21232d", fg="#d0d4e0").pack(side=tk.LEFT)
            self.msa_filter_var = tk.StringVar()
            filter_ent = tk.Entry(search_row, textvariable=self.msa_filter_var, bg="#15161e", fg="#ffffff", insertbackground="#ffffff")
            filter_ent.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=6)
            filter_ent.bind("<KeyRelease>", lambda _e: self.refresh_msa_list())

            tcols = ("key", "text")
            self.msa_tree = ttk.Treeview(tab_text, columns=tcols, show="headings", height=14)
            self.msa_tree.heading("key", text="MSA Key")
            self.msa_tree.column("key", width=110, anchor=tk.W)
            self.msa_tree.heading("text", text="Localized String")
            self.msa_tree.column("text", width=330, anchor=tk.W)
            self.msa_tree.pack(fill=tk.BOTH, expand=True, pady=6)
            self.msa_tree.bind("<<TreeviewSelect>>", self.on_msa_select)

            edit_box = tk.LabelFrame(tab_text, text="Edit Selected String", bg="#21232d", fg="#9ec5fe", padx=8, pady=6)
            edit_box.pack(fill=tk.X, pady=4)

            self.sel_msa_key_var = tk.StringVar(value="")
            tk.Label(edit_box, textvariable=self.sel_msa_key_var, bg="#21232d", fg="#ffd966", font=("Segoe UI", 9, "bold")).pack(anchor=tk.W)
            self.sel_msa_text_var = tk.StringVar(value="")
            tk.Entry(edit_box, textvariable=self.sel_msa_text_var, bg="#15161e", fg="#ffffff", insertbackground="#ffffff").pack(fill=tk.X, pady=4)

            btn_row = tk.Frame(edit_box, bg="#21232d")
            btn_row.pack(fill=tk.X, pady=2)
            tk.Button(btn_row, text="Update String & Preview", command=self.on_apply_msa_string,
                      bg="#3b6e52", fg="#ffffff", relief=tk.FLAT, padx=10, pady=4).pack(side=tk.LEFT, padx=2)
            tk.Button(btn_row, text="Save select.msa...", command=self.on_save_msa,
                      bg="#2b5b84", fg="#ffffff", relief=tk.FLAT, padx=10, pady=4).pack(side=tk.LEFT, padx=6)

            self.status_var = tk.StringVar(value="Ready. Open any .swa file (e.g. MainSelect_16x9.swa, AircraftSelect.swa, Brightness.swa) to begin.")
            tk.Label(self, textvariable=self.status_var, bg="#14151b", fg="#9ec5fe", anchor=tk.W, padx=8, pady=4).pack(
                side=tk.BOTTOM, fill=tk.X
            )

        def get_canvas_offset(self) -> Tuple[float, float]:
            cw = (self.doc.canvas_w if self.doc else 512) * self.zoom
            return (490.0 - cw / 2.0 + self.pan_x, 46.0 + self.pan_y)

        # Zoom & Pan handlers
        def on_zoom_in(self):
            self._step_zoom(1)

        def on_zoom_out(self):
            self._step_zoom(-1)

        def on_zoom_cb(self, _event=None):
            txt = self.zoom_var.get().rstrip("%")
            try:
                self.zoom = max(0.25, min(4.0, float(txt) / 100.0))
                self.redraw_canvas()
            except ValueError:
                pass

        def on_reset_view(self):
            self.zoom = 1.0
            self.pan_x = 0.0
            self.pan_y = 0.0
            self.zoom_var.set("100%")
            self.redraw_canvas()

        def on_mousewheel_zoom(self, event):
            direction = 1 if event.delta > 0 else -1
            self._step_zoom(direction, event.x, event.y)

        def _step_zoom(self, direction: int, pivot_x: float = None, pivot_y: float = None):
            cur_idx = min(range(len(self.ZOOM_LEVELS)), key=lambda i: abs(self.ZOOM_LEVELS[i] - self.zoom))
            new_idx = max(0, min(len(self.ZOOM_LEVELS) - 1, cur_idx + direction))
            new_zoom = self.ZOOM_LEVELS[new_idx]
            if abs(new_zoom - self.zoom) < 1e-4:
                return
            if pivot_x is not None and pivot_y is not None:
                ox, oy = self.get_canvas_offset()
                rel_x = (pivot_x - ox) / self.zoom
                rel_y = (pivot_y - oy) / self.zoom
                self.zoom = new_zoom
                new_ox, new_oy = self.get_canvas_offset()
                self.pan_x += pivot_x - (new_ox + rel_x * self.zoom)
                self.pan_y += pivot_y - (new_oy + rel_y * self.zoom)
            else:
                self.zoom = new_zoom
            self.zoom_var.set(f"{int(self.zoom * 100)}%")
            self.redraw_canvas()

        def on_pan_start(self, event):
            self.pan_start = (event.x, event.y, self.pan_x, self.pan_y)

        def on_pan_drag(self, event):
            if not self.pan_start:
                return
            sx, sy, px0, py0 = self.pan_start
            self.pan_x = px0 + (event.x - sx)
            self.pan_y = py0 + (event.y - sy)
            self.redraw_canvas()

        def on_open(self):
            path = filedialog.askopenfilename(
                title="Open SWAD (.swa) or SWG (.swg)",
                filetypes=[("ACZ Menu Files", "*.swa *.swg"), ("All Files", "*.*")]
            )
            if path:
                self.load_path(path)

        def on_open_msa(self):
            path = filedialog.askopenfilename(
                title="Open select.msa Localization Archive",
                filetypes=[("MSA Archive", "*.msa"), ("All Files", "*.*")]
            )
            if path:
                try:
                    self.msa = MsaArchive(path)
                    self.refresh_msa_list()
                    self.redraw_canvas()
                    self.status_var.set(f"Loaded {len(self.msa.key_to_loc)} localized strings from {os.path.basename(path)}")
                except Exception as e:
                    messagebox.showerror("MSA Load Error", str(e))

        def _populate_mc_and_labels(self):
            if not self.doc:
                return
            mc_list = self.doc.get_movieclip_list()
            mc_strs = [desc for _, desc in mc_list]
            self.mc_cb.configure(values=mc_strs)
            for idx, (did, _) in enumerate(mc_list):
                if did == self.doc.main_mc_id:
                    self.mc_cb.current(idx)
                    break

            fcount = self.doc.root_mc.get("fcount", 1)
            self.frame_slider.configure(to=max(0, fcount - 1))

            labels = [f"{name} (f={f})" for name, f in self.doc.root_mc.get("labels", [])]
            self.label_cb.configure(values=labels)
            if labels:
                self.label_cb.current(0)

            main_lbl_f = next((f for n, f in self.doc.root_mc.get("labels", []) if n == "main"), 0)
            default_frame = self._find_settled_frame(main_lbl_f)
            self.frame_slider.set(default_frame)
            self.current_frame = default_frame
            self.selected_slot = None

        def load_path(self, path: str):
            try:
                self.doc = SwgDocument.load_file(path)
            except Exception as e:
                messagebox.showerror("Load Error", str(e))
                return

            if self.doc.canvas_w >= 850:
                self.aspect_var.set("21:9 (896x448)")
            elif self.doc.canvas_w >= 650:
                self.aspect_var.set("16:9 (682x448)")
            else:
                self.aspect_var.set("4:3 (512x448)")

            self._populate_mc_and_labels()
            self.refresh_view()
            self.refresh_msa_list()
            fcount = self.doc.root_mc.get("fcount", 1)
            self.status_var.set(
                f"Loaded {os.path.basename(path)} | Menu='{self.doc.menu_name}' | Canvas={self.doc.canvas_w}x{self.doc.canvas_h} "
                f"| Textures={len(self.doc.textures)} | ActiveMC=Dict[{self.doc.main_mc_id}] ({fcount} frames)"
            )

        def on_mc_selected(self, _event=None):
            if not self.doc:
                return
            txt = self.mc_var.get()
            if txt.startswith("Dict["):
                did = int(txt.split("]")[0].replace("Dict[", ""))
                self.doc.set_active_movieclip(did)
                self._populate_mc_and_labels()
                self.refresh_view()

        def on_save(self):
            if not self.doc:
                return
            path = filedialog.asksaveasfilename(
                title="Save SWAD (.swa) or SWG (.swg)",
                initialfile=os.path.basename(self.doc.source_path or f"{self.doc.menu_name}_16x9.swa"),
                filetypes=[("SWAD Archive", "*.swa"), ("SWG Layout", "*.swg")]
            )
            if path:
                self.doc.save_to(path)
                self.status_var.set(f"Saved -> {path}")

        def on_aspect_changed(self, _event=None):
            if not self.doc:
                return
            val = self.aspect_var.get()
            if "21:9" in val:
                self.doc.set_canvas_width(896)
            elif "16:9" in val:
                self.doc.set_canvas_width(682)
            else:
                self.doc.set_canvas_width(512)
            self.redraw_canvas()

        def on_auto_widescreen(self, aspect: str):
            if not self.doc:
                return
            try:
                from ACZ_SWAD_Converter import patch_swg_to_widescreen
                new_bytes = patch_swg_to_widescreen(bytes(self.doc.swg), aspect=aspect)
                self.doc = SwgDocument(new_bytes, self.doc.source_path, self.doc.swa_chunks)
                self.aspect_var.set("21:9 (896x448)" if aspect == "21x9" else "16:9 (682x448)")
                self._populate_mc_and_labels()
                self.refresh_view()
                self.status_var.set(f"Applied automatic {aspect} widescreen layout conversion.")
            except Exception as e:
                messagebox.showerror("Auto-Layout Error", str(e))

        def _find_settled_frame(self, start_f: int) -> int:
            frames = self.doc.root_mc.get("frames", [])
            if not frames:
                return start_f
            labels = sorted(f for _, f in self.doc.root_mc.get("labels", []))
            next_f = next((lf for lf in labels if lf > start_f), min(len(frames), start_f + 45))
            best_f = start_f
            best_score = (-1, -1)
            for fi in range(start_f, min(len(frames), next_f)):
                vis_cnt = sum(1 for c in frames[fi] if self.doc.get_color_alpha(c.get("col_idx", -1)) > 0.5)
                tot_cnt = len(frames[fi])
                if (vis_cnt, tot_cnt) >= best_score:
                    best_score = (vis_cnt, tot_cnt)
                    best_f = fi
            return best_f

        def on_label_selected(self, _event=None):
            if not self.doc:
                return
            txt = self.label_var.get()
            if "(f=" in txt:
                f = int(txt.split("(f=")[1].rstrip(")"))
                self.frame_slider.set(self._find_settled_frame(f))

        def on_frame_slider(self, val):
            self.current_frame = int(val)
            self.refresh_view()

        def get_active_children(self):
            if not self.doc or not self.doc.root_mc:
                return []
            frames = self.doc.root_mc.get("frames", [])
            if not frames:
                return []
            f_idx = min(self.current_frame, len(frames) - 1)
            return frames[f_idx]

        def refresh_view(self):
            self.tree.delete(*self.tree.get_children())
            for c in self.get_active_children():
                if self.doc.get_color_alpha(c.get("col_idx", -1)) <= 0.01:
                    continue
                did = c["child_id"]
                m00, m01, m10, m11, tx, ty = self.doc.get_matrix(c["mat_idx"])
                sname = c.get("sym_name") or f"Dict[{did}]"
                self.tree.insert(
                    "", tk.END, iid=str(c["slot"]),
                    values=(c["slot"], did, sname, c["mat_idx"], f"{tx:.1f}", f"{ty:.1f}", f"{m00:.2f}", f"{m11:.2f}")
                )
            if self.selected_slot is not None and self.tree.exists(str(self.selected_slot)):
                self.tree.selection_set(str(self.selected_slot))
            self.redraw_canvas()

        def _get_resize_handles(self, bx0, by0, bx1, by1) -> Dict[str, Tuple[float, float]]:
            mx = (bx0 + bx1) / 2.0
            my = (by0 + by1) / 2.0
            return {
                "NW": (bx0, by0), "N": (mx, by0), "NE": (bx1, by0),
                "E":  (bx1, my),  "SE": (bx1, by1), "S": (mx, by1),
                "SW": (bx0, by1), "W":  (bx0, my)
            }

        def redraw_canvas(self):
            self.canvas.delete("all")
            if not self.doc:
                return

            cw, ch = self.doc.canvas_w, self.doc.canvas_h
            z = self.zoom
            zcw, zch = int(round(cw * z)), int(round(ch * z))
            ox, oy = self.get_canvas_offset()
            x_4_3 = ox + ((cw - 512.0) / 2.0) * z

            # Render full in-game PIL scene (with PS2 GS additive blending)
            pil_img, self.hit_boxes = self.doc.render_scene_to_pil(self.current_frame, self.msa)
            if self.show_tex_var.get():
                if abs(z - 1.0) > 1e-3:
                    pil_img = pil_img.resize((zcw, zch), Image.Resampling.BILINEAR)
                self.tk_img_ref = ImageTk.PhotoImage(pil_img)
                self.canvas.create_image(ox, oy, anchor=tk.NW, image=self.tk_img_ref)
            else:
                self.canvas.create_rectangle(ox, oy, ox + zcw, oy + zch, fill="#141720", outline="")

            # Outer viewport border & 4:3 safe-frame guide
            self.canvas.create_rectangle(ox, oy, ox + zcw, oy + zch, outline="#3d85c6", width=2)
            self.canvas.create_rectangle(x_4_3, oy, x_4_3 + 512.0 * z, oy + zch, outline="#777777", dash=(4, 4))
            self.canvas.create_text(
                ox + 4, oy - 14, anchor=tk.W, fill="#9ec5fe",
                text=f"Viewport: {cw}x{ch} (Zoom {int(z*100)}%) | LMB: Select/Move/Resize | RMB/MMB: Pan | Wheel: Zoom"
            )

            # Draw wireframe bounding boxes & 8 resize handles on selected item
            for hb in self.hit_boxes:
                c = hb["placement"]
                x0, y0, x1, y1 = hb["bbox_canvas"]
                sx0, sy0, sx1, sy1 = ox + x0 * z, oy + y0 * z, ox + x1 * z, oy + y1 * z
                is_sel = (self.selected_slot == c["slot"])

                if self.show_wire_var.get() or is_sel:
                    outline = "#ffd966" if is_sel else "#4ea8de"
                    width = 2 if is_sel else 1
                    self.canvas.create_rectangle(sx0, sy0, sx1, sy1, outline=outline, width=width)
                    lbl = c.get("sym_name") or f"Dict[{c['child_id']}]"
                    self.canvas.create_text(sx0 + 3, sy0 + 2, anchor=tk.NW, fill=outline, text=lbl, font=("Segoe UI", 8))

                if is_sel:
                    r = self.HANDLE_RADIUS
                    for hname, (hx, hy) in self._get_resize_handles(sx0, sy0, sx1, sy1).items():
                        self.canvas.create_rectangle(
                            hx - r, hy - r, hx + r, hy + r,
                            fill="#ffd966", outline="#181920", width=1
                        )

        def _populate_inspector_from_slot(self, slot: int):
            hb = next((h for h in self.hit_boxes if h["placement"]["slot"] == slot), None)
            if not hb:
                return
            c = hb["placement"]
            m00, m01, m10, m11, tx, ty = self.doc.get_matrix(c["mat_idx"])
            self.mat_vars["m00"].set(f"{m00:.4f}")
            self.mat_vars["m01"].set(f"{m01:.4f}")
            self.mat_vars["m10"].set(f"{m10:.4f}")
            self.mat_vars["m11"].set(f"{m11:.4f}")
            self.mat_vars["tx"].set(f"{tx:.2f}")
            self.mat_vars["ty"].set(f"{ty:.2f}")

        def on_tree_select(self, _event=None):
            sel = self.tree.selection()
            if not sel or not self.doc:
                return
            self.selected_slot = int(sel[0])
            self._populate_inspector_from_slot(self.selected_slot)
            self.redraw_canvas()

        def on_apply_matrix(self):
            if not self.doc or self.selected_slot is None:
                return
            hb = next((h for h in self.hit_boxes if h["placement"]["slot"] == self.selected_slot), None)
            if not hb:
                return
            try:
                m00 = float(self.mat_vars["m00"].get())
                m01 = float(self.mat_vars["m01"].get())
                m10 = float(self.mat_vars["m10"].get())
                m11 = float(self.mat_vars["m11"].get())
                tx  = float(self.mat_vars["tx"].get())
                ty  = float(self.mat_vars["ty"].get())
                self.doc.set_matrix(hb["placement"]["mat_idx"], m00, m01, m10, m11, tx, ty)
                self.refresh_view()
            except ValueError as e:
                messagebox.showerror("Invalid Number", str(e))

        def on_canvas_press(self, event):
            if not self.doc:
                return
            ox, oy = self.get_canvas_offset()
            z = self.zoom

            # 1. Check if clicking one of the 8 resize handles of the currently selected item
            if self.selected_slot is not None:
                sel_hb = next((h for h in self.hit_boxes if h["placement"]["slot"] == self.selected_slot), None)
                if sel_hb:
                    x0, y0, x1, y1 = sel_hb["bbox_canvas"]
                    sx0, sy0, sx1, sy1 = ox + x0 * z, oy + y0 * z, ox + x1 * z, oy + y1 * z
                    for hname, (hx, hy) in self._get_resize_handles(sx0, sy0, sx1, sy1).items():
                        if abs(event.x - hx) <= self.HANDLE_RADIUS + 3 and abs(event.y - hy) <= self.HANDLE_RADIUS + 3:
                            c = sel_hb["placement"]
                            m00, m01, m10, m11, tx, ty = self.doc.get_matrix(c["mat_idx"])
                            self.drag_mode = hname
                            self.drag_state = (event.x, event.y, m00, m01, m10, m11, tx, ty, sel_hb["local_bbox"])
                            return

            # 2. Hit-test active bounding boxes in unzoomed canvas coordinates
            cx, cy = (event.x - ox) / z, (event.y - oy) / z
            candidates = []
            for hb in self.hit_boxes:
                x0, y0, x1, y1 = hb["bbox_canvas"]
                if x0 <= cx <= x1 and y0 <= cy <= y1:
                    area = max(1.0, (x1 - x0) * (y1 - y0))
                    candidates.append((area, -hb["placement"]["slot"], hb))
            if candidates:
                candidates.sort(key=lambda t: (t[0], t[1]))
                chosen_hb = candidates[0][2]
                c = chosen_hb["placement"]
                self.selected_slot = c["slot"]
                if self.tree.exists(str(self.selected_slot)):
                    self.tree.selection_set(str(self.selected_slot))
                self._populate_inspector_from_slot(self.selected_slot)
                m00, m01, m10, m11, tx, ty = self.doc.get_matrix(c["mat_idx"])
                self.drag_mode = "move"
                self.drag_state = (event.x, event.y, m00, m01, m10, m11, tx, ty, chosen_hb["local_bbox"])
                self.redraw_canvas()

        def on_canvas_drag(self, event):
            if not self.doc or self.selected_slot is None or not self.drag_mode or not self.drag_state:
                return
            hb = next((h for h in self.hit_boxes if h["placement"]["slot"] == self.selected_slot), None)
            if not hb:
                return
            c = hb["placement"]
            sx, sy, m00, m01, m10, m11, tx, ty, (lx0, ly0, lx1, ly1) = self.drag_state
            dx = (event.x - sx) / self.zoom
            dy = (event.y - sy) / self.zoom
            lw = max(4.0, lx1 - lx0)
            lh = max(4.0, ly1 - ly0)

            new_m00, new_m11, new_tx, new_ty = m00, m11, tx, ty

            if self.drag_mode == "move":
                new_tx = tx + dx
                new_ty = ty + dy
            else:
                if "E" in self.drag_mode:
                    new_m00 = max(0.05, m00 + dx / lw)
                    new_tx = tx + lx0 * (m00 - new_m00)
                elif "W" in self.drag_mode:
                    new_m00 = max(0.05, m00 - dx / lw)
                    new_tx = tx + lx1 * (m00 - new_m00)

                if "S" in self.drag_mode:
                    new_m11 = max(0.05, m11 + dy / lh)
                    new_ty = ty + ly0 * (m11 - new_m11)
                elif "N" in self.drag_mode:
                    new_m11 = max(0.05, m11 - dy / lh)
                    new_ty = ty + ly1 * (m11 - new_m11)

            self.doc.set_matrix(c["mat_idx"], new_m00, m01, m10, new_m11, new_tx, new_ty)
            self.mat_vars["m00"].set(f"{new_m00:.4f}")
            self.mat_vars["m11"].set(f"{new_m11:.4f}")
            self.mat_vars["tx"].set(f"{new_tx:.2f}")
            self.mat_vars["ty"].set(f"{new_ty:.2f}")
            self.redraw_canvas()

        def on_canvas_release(self, _event):
            if self.drag_mode:
                self.drag_mode = None
                self.drag_state = None
                self.refresh_view()

        # MSA String Localization Tab Handlers
        def refresh_msa_list(self):
            self.msa_tree.delete(*self.msa_tree.get_children())
            if not self.msa:
                return
            flt = self.msa_filter_var.get().strip().lower()
            menu_keys = set()
            if self.doc:
                for s in self.doc.strings_pool:
                    if s in self.msa.key_to_loc:
                        menu_keys.add(s)

            ordered_keys = sorted(menu_keys)
            if flt:
                for k in sorted(self.msa.key_to_loc.keys()):
                    if k not in menu_keys and (flt in k.lower() or flt in self.msa.get(k).lower()):
                        ordered_keys.append(k)
                        if len(ordered_keys) >= 300:
                            break
            elif not ordered_keys:
                ordered_keys = sorted(self.msa.key_to_loc.keys())[:200]

            for k in ordered_keys:
                txt = self.msa.get(k)
                if not flt or (flt in k.lower() or flt in txt.lower()):
                    self.msa_tree.insert("", tk.END, iid=k, values=(k, txt))

        def on_msa_select(self, _event=None):
            sel = self.msa_tree.selection()
            if not sel or not self.msa:
                return
            k = sel[0]
            self.sel_msa_key_var.set(f"Key: {k}")
            self.sel_msa_text_var.set(self.msa.get(k))

        def on_apply_msa_string(self):
            if not self.msa:
                return
            sel = self.msa_tree.selection()
            if not sel:
                return
            k = sel[0]
            new_txt = self.sel_msa_text_var.get()
            self.msa.set_string(k, new_txt)
            self.msa_tree.item(k, values=(k, new_txt))
            self.redraw_canvas()
            self.status_var.set(f"Updated MSA key '{k}' -> {new_txt!r}")

        def on_save_msa(self):
            if not self.msa:
                return
            path = filedialog.asksaveasfilename(
                title="Save select.msa Localization Archive",
                initialfile="select.msa",
                filetypes=[("MSA Archive", "*.msa"), ("All Files", "*.*")]
            )
            if path:
                self.msa.save(path)
                self.status_var.set(f"Saved modified select.msa -> {path}")

    app = SwgVisualEditorApp(initial_path)
    app.mainloop()


if __name__ == "__main__":
    init_file = sys.argv[1] if len(sys.argv) > 1 else None
    launch_tkinter_editor(init_file)
