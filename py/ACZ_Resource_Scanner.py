#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ace Combat Zero: The Belkan War — Fast Resource Scanner & Master Glyph Atlas Builder (v9)
=========================================================================================
Scans unpacked game archives (a.arc and d.arc) and builds unified per-language, per-dimension
Master Glyph Atlases (separately for 32x32, 16x16, etc.), merging English variants (EN_US and EN_UK)
into a single unified EN folder:

  1. Complete Container & Stream Coverage:
     - Scans .FHM, .MSA (main menu & hangar: data/select/select.msa), .BFN (briefings: data/brdata/*.bfn),
       .BIN (FMV subtitles data/caption/movie/*, cutscenes & squadron intros data/caption/dd/*,
       debriefings data/caption/debri/*, in-flight radio dialogue data/mis/pack/*), .DAT, .PAC, .TBL, etc.
     - Detects both standalone and concatenated/embedded GameFont blocks (32x32 with log2=5, 16x16 with
       log2=4, 64x64 with log2=6) at arbitrary 16-byte-aligned offsets inside binary streams.
     - Properly parses FHM containers with empty slots (offset=0) and count % 4 == 3 (including 7-slot
       language containers where the first child starts immediately at 4 * (count + 1)).
  2. Unified English Localization:
     - Merges all resources from 1us / 2uk / us / uk / EN_US / EN_UK into a single EN folder.
     - Separates other languages into dedicated folders: DE, FR, ES, IT, JP, RU, COMMON.
     - Automatically merges shared COMMON glyphs into each active language's Master Atlas so that
       a single MASTER_EN_font_32x32.png covers 100% of 32x32 text in the English game.
  3. Per-Dimension Master Atlases (32x32, 16x16, etc.):
     - All 32x32 dynamic font glyphs for a language are combined into:
         SCAN_OUT/<LANG>/fonts/MASTER_<LANG>_font_32x32.png (+ .txt)
     - All 16x16 dynamic font glyphs for a language are combined into:
         SCAN_OUT/<LANG>/fonts/MASTER_<LANG>_font_16x16.png (+ .txt)
     - All 16x16 GIM icon/glyph textures are combined into:
         SCAN_OUT/<LANG>/fonts/MASTER_<LANG>_gim_16x16.png  (+ .txt)
     - Copies of all Master Atlases are also placed in SCAN_OUT/_MASTER_ATLASES/ for quick access.
  4. Transitive Typeface Clustering & Collision-Free Deduplication:
     - Multi-pass transitive family consolidation merges 1-line movie subtitles, briefing lines,
       coordinates, and enemy squadron intros into contiguous typeface sections.
     - Master Atlas deduplication across different typefaces uses ExactHash (including the 64-byte
       PS2 CLUT palette) so characters with identical uncolored shapes in different fonts (e.g.
       Menu Descriptions vs. Briefing Subtitles) are never stripped from their typeface section.

Usage:
  python ACZ_Resource_Scanner.py [extracted_dir] [output_dir] [options]

Options:
  --lang LANG           Dump only the specified language(s) (e.g.: --lang EN or --lang EN,JP)
  --dump-families       Also export individual merged typeface files FAMILY_<HASH>.png
  --dump-individuals    Also export individual font block files <HASH>_font_*.png
  --include-subtitles   When using --dump-individuals, do not skip redundant 1-line subtitle blocks
  --all-textures        Export all GIM textures in the game (not only fonts and 16x16 icons)

Version History:
  - v1..v2: Initial archive scanner for GIM textures and standalone .fhm fonts.
  - v3..v4: Added arbitrary 16-byte-aligned raw stream scanning across .MSA, .BFN,
            .BIN, .DAT, .PAC containers and FHM empty-slot / count % 4 == 3 support.
  - v5    : Added Font Family clustering (FAMILY_*) and per-language folder organization
            (EN, DE, FR, ES, IT, JP, RU, COMMON).
  - v6    : Merged EN_US + EN_UK (1us, 2uk, us, uk) into a single unified EN folder and
            merged shared COMMON glyphs into each active language's Master Atlas.
  - v7    : Added per-dimension Master Font Atlases (MASTER_<LANG>_font_32x32.png,
            MASTER_<LANG>_font_16x16.png) and Master 16x16 GIM Atlases
            (MASTER_<LANG>_gim_16x16.png), multi-pass transitive family consolidation,
            and collision-free ExactHash deduplication across typefaces.
  - v8    : Added MASTER_<LANG>_font_64x64.png + .txt (log2=6) and visual CLUT-variant
            deduplication with AltHashes= output.
  - v9    : Added unified per-family baseline height (std_h) vertical centering in
            clean_restore_and_center_1x_tile and idempotency guard.
"""

import argparse
import os
import re
import shutil
import struct
import sys
from typing import Dict, List, Optional, Set, Tuple

import numpy as np
from PIL import Image

SCANNER_VERSION = "v9"
DEFAULT_EXTRACTED_DIR = "extracted"
DEFAULT_SCAN_OUT_DIR = "SCAN_OUT"
GIM_MAGIC = b"GIM\x00"

FNV1A_64_OFFSET = 0xCBF29CE484222325
FNV1A_64_PRIME = 0x100000001B3

# File extensions that are guaranteed not to contain binary ACZ font/texture containers
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
}

# Slot indices in 7-slot language .FHM containers (switch at 0x1400EC560 in acz.exe).
# English variants (slot 1 = EN_US and slot 2 = EN_UK) are merged into a single "EN" tag.
PMENU_LANG_TAGS = {
    0: "JP",
    1: "EN",
    2: "EN",
    3: "IT",
    4: "DE",
    5: "FR",
    6: "ES",
}

# Mapping of directory names from d/lang/* and a/data/caption/* to canonical language codes.
# All English variants (1us, 2uk, us, uk, en_us, en_uk, eng, en) map to "EN".
PATH_SEGMENT_TO_LANG = {
    # d.arc (d/lang/*)
    "0jp": "JP",
    "1us": "EN",
    "2uk": "EN",
    "3ge": "DE",
    "4fr": "FR",
    "5it": "IT",
    "6es": "ES",
    # a.arc (a/data/caption/dd, debri, movie)
    "jp": "JP",
    "ej": "JP",
    "us": "EN",
    "uk": "EN",
    "gr": "DE",
    "ge": "DE",
    "de": "DE",
    "fr": "FR",
    "it": "IT",
    "sp": "ES",
    "es": "ES",
    # Additional general tags
    "jpn": "JP",
    "ja": "JP",
    "en_us": "EN",
    "en_uk": "EN",
    "eng": "EN",
    "en": "EN",
    "ger": "DE",
    "deu": "DE",
    "fra": "FR",
    "fre": "FR",
    "ita": "IT",
    "spa": "ES",
    "esp": "ES",
    "ru": "RU",
    "rus": "RU",
}

LANG_DISPLAY_ORDER = [
    "EN",
    "DE",
    "FR",
    "ES",
    "IT",
    "JP",
    "RU",
    "COMMON",
]

FONT_KEYWORDS = (
    "font",
    "fnt",
    "glyph",
    "ascii",
    "kanji",
    "moji",
    "text",
    "subt",
    "pmenu",
    "caption",
    "debri",
    "brdata",
    "select",
    "brief",
    "radio",
    "title",
    "menu",
)


def normalize_lang_code(lang: str) -> str:
    """Normalizes a language tag to its canonical folder code (EN_US / EN_UK / US / UK -> EN)."""
    u = lang.strip().upper()
    if u in ("EN_US", "EN_UK", "US", "UK", "ENG", "EN", "1US", "2UK"):
        return "EN"
    if u in ("JP", "JPN", "JA", "EJ", "0JP"):
        return "JP"
    if u in ("DE", "GE", "GR", "GER", "DEU", "3GE"):
        return "DE"
    if u in ("FR", "FRA", "FRE", "4FR"):
        return "FR"
    if u in ("IT", "ITA", "5IT"):
        return "IT"
    if u in ("ES", "SP", "SPA", "ESP", "6ES"):
        return "ES"
    if u in ("RU", "RUS"):
        return "RU"
    return u


def fnv1a64_bytes(buf: bytes) -> int:
    """Computes standard 64-bit FNV-1a hash over a byte buffer."""
    h = FNV1A_64_OFFSET
    prime = FNV1A_64_PRIME
    for b in buf:
        h = ((h ^ b) * prime) & 0xFFFFFFFFFFFFFFFF
    return h


def calc_gim_hashes(gim_data: bytes, clut_off: int) -> Tuple[int, int, int, int]:
    """
    Computes the 4 variants of the 64-bit FNV-1a GIM\\0 hash (matching SafeInspectAndHashGim in acz_hdtex.cpp):
      - hash_psm_norm (primary hash: byte 0x12=0, and 0x13: 0x1B->0x13)
      - hash_exact    (byte 0x12=0)
      - hash_fhm_norm (bytes 0x10..0x12=0, 0x13: 0x1B->0x13, clut_off..clut_off+2=0)
      - hash_raw      (unmodified bytes)
    """
    b_raw = bytearray(gim_data)
    b_exact = bytearray(gim_data)
    b_psm = bytearray(gim_data)
    b_fhm = bytearray(gim_data)

    if len(gim_data) > 0x13:
        b_exact[0x12] = 0
        b_psm[0x12] = 0
        if b_psm[0x13] == 0x1B:
            b_psm[0x13] = 0x13
        b_fhm[0x10] = 0
        b_fhm[0x11] = 0
        b_fhm[0x12] = 0
        b_fhm[0x13] = b_psm[0x13]

    if clut_off + 3 <= len(gim_data):
        b_fhm[clut_off] = 0
        b_fhm[clut_off + 1] = 0
        b_fhm[clut_off + 2] = 0

    h_psm_norm = fnv1a64_bytes(b_psm)
    h_exact = fnv1a64_bytes(b_exact)
    h_fhm_norm = fnv1a64_bytes(b_fhm)
    h_raw = fnv1a64_bytes(b_raw)
    return h_psm_norm, h_exact, h_fhm_norm, h_raw


def calc_glyph_tile_hash(
    glyph_dim: int,
    char_code: int,
    adv_x: int,
    height: int,
    tile_bytes: bytes,
    clut_64: Optional[bytes] = None,
) -> int:
    """
    Exact equivalent of ComputeGlyphTileHash in acz_hdtex.cpp.
    When clut_64 is None, computes bitmap_tile_hash (written as TileHash= in .txt).
    When clut_64 is provided, computes exact_tile_hash (written as ExactHash= in .txt).
    """
    hdr = struct.pack("<HHHH", glyph_dim & 0xFFFF, char_code & 0xFFFF, adv_x & 0xFFFF, height & 0xFFFF)
    if clut_64 is not None:
        return fnv1a64_bytes(hdr + clut_64[:64] + tile_bytes)
    return fnv1a64_bytes(hdr + tile_bytes)


def calc_dynamic_font_hashes(data: bytes) -> Tuple[int, int, int, int, int, int, int]:
    """
    Computes 64-bit FNV-1a hashes for a dynamic tile font (GameFont / CUnicodeFont / Pmenu),
    matching SafeInspectAndHashGameFont in acz_hdtex.cpp.
    Returns (h_norm, h_clean, h_vis, rdi_off, r10_off, end_idx_off, pal_off).
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

    h_norm = fnv1a64_bytes(b_norm)
    h_clean = fnv1a64_bytes(b_clean)
    vis_buf = struct.pack("<HH", num_glyphs, glyph_dim) + data[pal_off:total_size]
    h_vis = fnv1a64_bytes(vis_buf)
    return h_norm, h_clean, h_vis, rdi_off, r10_off, end_idx_off, pal_off


def detect_lang_from_path(rel_path: str) -> Optional[str]:
    """Detects the localization language code from a file's relative path in unpacked a.arc / d.arc."""
    norm = rel_path.lower().replace("\\", "/")
    parts = [p for p in norm.split("/") if p]

    for part in parts[:-1]:
        if part in PATH_SEGMENT_TO_LANG:
            return PATH_SEGMENT_TO_LANG[part]

    stem = os.path.splitext(parts[-1])[0] if parts else ""
    for suffix, lang in (
        ("_en_us", "EN"),
        ("_en_uk", "EN"),
        ("_us", "EN"),
        ("_uk", "EN"),
        ("_en", "EN"),
        ("_jp", "JP"),
        ("_ja", "JP"),
        ("_de", "DE"),
        ("_ge", "DE"),
        ("_gr", "DE"),
        ("_fr", "FR"),
        ("_it", "IT"),
        ("_es", "ES"),
        ("_sp", "ES"),
        ("_ru", "RU"),
    ):
        if stem.endswith(suffix):
            return lang

    return None


EN_VOCAB = {
    "the",
    "and",
    "for",
    "with",
    "that",
    "this",
    "from",
    "your",
    "are",
    "not",
    "all",
    "through",
    "destroy",
    "shoot",
    "aircraft",
    "switch",
    "between",
    "controls",
    "loading",
    "more",
    "cancel",
    "select",
    "mission",
    "campaign",
    "options",
    "controller",
    "display",
    "sound",
    "data",
    "save",
    "load",
    "free",
    "trial",
    "scene",
    "viewer",
    "music",
    "player",
    "flight",
    "record",
    "assault",
    "records",
    "medal",
    "hangar",
    "buy",
    "sell",
    "fighter",
    "attacker",
    "multirole",
    "jammer",
    "normal",
    "novice",
    "expert",
    "ace",
    "very",
    "easy",
    "hard",
    "start",
    "return",
    "quit",
    "retry",
    "yes",
    "no",
    "air",
    "force",
    "division",
    "unit",
    "squadron",
    "tactical",
    "base",
    "valais",
    "ustio",
    "belkan",
    "belka",
    "osean",
    "osea",
    "sapin",
    "recta",
    "galm",
    "pixy",
    "cipher",
    "pj",
    "eagle",
    "crow",
    "schwarze",
    "rot",
    "grun",
    "gelb",
    "silber",
    "gault",
    "sorcerer",
    "wizard",
    "espada",
    "enemy",
    "allied",
    "target",
    "targets",
    "briefing",
    "debriefing",
    "complete",
    "completed",
    "failed",
    "updated",
    "refueling",
    "landing",
    "takeoff",
    "altitude",
    "speed",
    "weapon",
    "special",
    "gun",
    "missile",
    "cockpit",
    "hud",
    "first",
    "person",
    "third",
    "vibration",
    "brightness",
    "line",
    "lines",
    "wingman",
    "attack",
    "cover",
    "dispersal",
}

DE_VOCAB = {
    "sie",
    "und",
    "der",
    "die",
    "das",
    "den",
    "dem",
    "nicht",
    "auf",
    "für",
    "mit",
    "durch",
    "bereich",
    "fliegen",
    "zerstören",
    "abschießen",
    "wählen",
    "zurück",
    "abbrechen",
    "flugzeug",
    "einsatz",
    "kampagne",
    "optionen",
    "speichern",
    "laden",
    "steuerung",
    "anzeige",
}

FR_VOCAB = {
    "les",
    "des",
    "dans",
    "une",
    "est",
    "pour",
    "sur",
    "pas",
    "plus",
    "avec",
    "vers",
    "abattez",
    "allez",
    "avion",
    "sélectionnez",
    "annuler",
    "retour",
    "campagne",
    "commandes",
    "affichage",
    "sauvegarder",
    "charger",
    "escadron",
}

ES_VOCAB = {
    "los",
    "las",
    "del",
    "una",
    "por",
    "con",
    "para",
    "como",
    "más",
    "pero",
    "sus",
    "hacia",
    "derriba",
    "avión",
    "selecciona",
    "cancelar",
    "volver",
    "campaña",
    "misión",
    "opciones",
    "guardar",
    "cargar",
    "escuadrón",
}

IT_VOCAB = {
    "della",
    "nella",
    "degli",
    "delle",
    "sul",
    "nel",
    "che",
    "non",
    "per",
    "con",
    "una",
    "sono",
    "abbatti",
    "raggiungi",
    "caccia",
    "seleziona",
    "annulla",
    "indietro",
    "campagna",
    "missione",
    "opzioni",
    "salva",
    "carica",
    "squadriglia",
}


def detect_lang_from_font_content(finfo: dict) -> str:
    """
    Infers the language of a dynamic font from its Unicode codepoints and decoded text strings
    when neither the archive path nor a 7-slot container explicitly specifies a language.
    All English blocks return the unified tag "EN".
    """
    char_codes = [g["char_code"] for g in finfo.get("glyphs", [])]

    if any((0x3000 <= cp <= 0x9FFF) or (0xFF00 <= cp <= 0xFFEF) for cp in char_codes):
        return "JP"

    if any(0x0400 <= cp <= 0x04FF for cp in char_codes):
        return "RU"

    all_text = " ".join(finfo.get("strings", []))
    chars_set = set(all_text)

    if "ß" in chars_set:
        return "DE"
    if "¿" in chars_set or "¡" in chars_set or "ñ" in chars_set or "Ñ" in chars_set:
        return "ES"
    if "œ" in chars_set or "Œ" in chars_set or "ç" in chars_set or "Ç" in chars_set:
        return "FR"

    words = set(re.findall(r"[A-Za-zÀ-ÿ]+", all_text.lower()))

    de_score = len(words & DE_VOCAB)
    fr_score = len(words & FR_VOCAB)
    es_score = len(words & ES_VOCAB)
    it_score = len(words & IT_VOCAB)
    en_score = len(words & EN_VOCAB)

    if any(c in chars_set for c in ("ä", "ö", "ü", "Ä", "Ö", "Ü")):
        de_score += 2
    if any(c in chars_set for c in ("é", "è", "ê", "à", "ù", "â", "î", "ô", "û")):
        fr_score += 1
    if any(c in chars_set for c in ("á", "í", "ó", "ú")):
        es_score += 1
    if any(c in chars_set for c in ("ì", "ò")):
        it_score += 1

    scores = [
        (de_score, "DE"),
        (fr_score, "FR"),
        (es_score, "ES"),
        (it_score, "IT"),
        (en_score, "EN"),
    ]
    scores.sort(key=lambda x: x[0], reverse=True)
    if scores[0][0] >= 1 and scores[0][0] > scores[1][0]:
        return scores[0][1]

    return "COMMON"


def csm1_swizzle_palette(palette: np.ndarray) -> np.ndarray:
    """PS2 GS CSM1 CLUT block permutation (for 8-bit indexed textures)."""
    out = palette.copy()
    num_colors = len(out)
    for base in range(0, num_colors, 32):
        if base + 24 <= num_colors:
            out[base + 8 : base + 16] = palette[base + 16 : base + 24]
            out[base + 16 : base + 24] = palette[base + 8 : base + 16]
    return out


def ps2_alpha_to_pc(a_ps2: np.ndarray) -> np.ndarray:
    """Exact integer conversion of PS2 alpha (0..128 -> 0..255), matching Ps2AlphaToPc in acz_hdtex.cpp."""
    v = (a_ps2.astype(np.uint32) * 255 + 64) // 128
    return np.clip(v, 0, 255).astype(np.uint8)


def try_parse_gim_at(buf: bytes, offset: int = 0) -> int:
    """
    Checks whether a valid GIM\\0 texture starts at `offset` in `buf`.
    Returns the exact GIM byte size (`total_size`) or 0 if not a valid GIM.
    """
    if offset < 0 or offset + 0x30 > len(buf):
        return 0
    if buf[offset : offset + 4] != GIM_MAGIC:
        return 0

    psm = buf[offset + 0x13]
    width, height = struct.unpack_from("<HH", buf, offset + 0x1C)
    if width == 0 or height == 0 or width > 4096 or height > 4096:
        return 0

    if psm in (0x13, 0x1B):
        raw_pix_len = width * height
        expected_colors = 256
    elif psm in (0x14, 0x24):
        raw_pix_len = (width * height + 1) // 2
        expected_colors = 16
    else:
        return 0

    aligned_pix_len = (raw_pix_len + 15) & ~15
    clut_off = 0x20 + aligned_pix_len
    if offset + clut_off + 0x10 > len(buf):
        return 0

    clut_w, clut_h = struct.unpack_from("<HH", buf, offset + clut_off + 0x0C)
    num_colors = clut_w * clut_h
    if num_colors != expected_colors:
        return 0

    total_size = clut_off + 0x10 + num_colors * 4
    if offset + total_size > len(buf):
        return 0

    return total_size


def decode_gim_bytes(gim_data: bytes) -> Tuple[Image.Image, dict]:
    """Decodes a GIM\\0 buffer into a PIL.Image and returns metadata with normalized hashes."""
    total_size = try_parse_gim_at(gim_data, 0)
    if total_size <= 0:
        raise ValueError("Invalid GIM\\0 header.")

    psm = gim_data[0x13]
    width, height = struct.unpack_from("<HH", gim_data, 0x1C)
    if psm in (0x13, 0x1B):
        bpp = 8
        raw_pix_len = width * height
    else:
        bpp = 4
        raw_pix_len = (width * height + 1) // 2

    aligned_pix_len = (raw_pix_len + 15) & ~15
    clut_off = 0x20 + aligned_pix_len
    clut_w, clut_h = struct.unpack_from("<HH", gim_data, clut_off + 0x0C)
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
    h_psm_norm, h_exact, h_fhm_norm, h_raw = calc_gim_hashes(gim_data[:exact_size], clut_off)
    return img, {
        "width": width,
        "height": height,
        "bpp": bpp,
        "psm": psm,
        "hash": f"{h_psm_norm:016X}",
        "hash_exact": f"{h_exact:016X}",
        "hash_fhm_norm": f"{h_fhm_norm:016X}",
        "total_size": exact_size,
    }


def try_parse_gamefont_at(buf: bytes, offset: int = 0) -> int:
    """
    Checks whether a valid dynamic tile font structure (GameFont / CSwgGameFont / CUnicodeFont / Pmenu,
    parsed at 0x1401FFBD0 in acz.exe) starts at `offset` in `buf`.
    Supports 32x32 fonts (unk06 == 5), 16x16 fonts (unk06 == 4), and 64x64 fonts (unk06 == 6),
    including concatenated streams inside .MSA, .BFN, .BIN, .DAT, and .PAC containers.
    Returns the exact byte size `total_size` (or 0 if not a valid GameFont).
    """
    buf_len = len(buf)
    if offset < 0 or offset + 0x60 > buf_len:
        return 0
    if buf[offset : offset + 4] == GIM_MAGIC:
        return 0

    num_strs, num_glyphs, unk06, glyph_dim, cbp, tbp, max_adv = struct.unpack_from("<IHHHHHH", buf, offset)
    if not (1 <= num_strs <= 4096 and 1 <= num_glyphs <= 4096):
        return 0
    if glyph_dim not in (12, 16, 20, 24, 32, 48, 64):
        return 0
    # unk06 is log2(glyph_dim): 4 for 16x16, 5 for 32x32, 6 for 64x64
    if (1 << unk06) != glyph_dim and unk06 not in (4, 5, 6):
        return 0
    if not (1 <= max_adv <= 128):
        return 0

    rdi_off = 16 * (num_glyphs + 1)
    r10_off = ((rdi_off + 2 * num_strs) & ~3) + 4
    if offset + r10_off + 4 * num_strs > buf_len:
        return 0

    first_off = struct.unpack_from("<i", buf, offset + r10_off)[0]
    if first_off != 0:
        return 0

    last_len = struct.unpack_from("<h", buf, offset + rdi_off + 2 * (num_strs - 1))[0]
    last_off = struct.unpack_from("<i", buf, offset + r10_off + 4 * (num_strs - 1))[0]
    if last_len < 0 or last_len > 2048 or last_off < 0 or last_off > 262144:
        return 0

    end_idx_off = r10_off + 2 * (last_off + 2 * num_strs + last_len)
    pal_off = (end_idx_off & ~15) + 16
    bmp_off = pal_off + 0x40
    bmp_size = (num_glyphs * glyph_dim * glyph_dim) // 2
    total_size = bmp_off + bmp_size

    if total_size > 8 * 1024 * 1024 or offset + total_size > buf_len:
        return 0

    # Validate glyph metric headers (adv_x, height, char_code)
    check_gl = min(num_glyphs, 8)
    max_metric = glyph_dim * 2
    for g in range(check_gl):
        adv_x, height, _, char_code = struct.unpack_from("<HHIH", buf, offset + 16 + g * 16)
        if adv_x > max_metric or height == 0 or height > max_metric or char_code == 0xFFFF:
            return 0

    # Validate string length and offset tables
    check_st = min(num_strs, 16)
    for s in range(check_st):
        slen = struct.unpack_from("<h", buf, offset + rdi_off + 2 * s)[0]
        soff = struct.unpack_from("<i", buf, offset + r10_off + 4 * s)[0]
        if slen < 0 or slen > 2048 or soff < 0 or soff > last_off:
            return 0

    return total_size


def is_dynamic_gamefont_entry(data: bytes) -> bool:
    """Checks if `data` is a standalone GameFont buffer starting at offset 0."""
    total_size = try_parse_gamefont_at(data, 0)
    return total_size > 0 and total_size <= len(data) <= total_size + 64


def format_cp_utf8(cp: int) -> str:
    """Formats a Unicode codepoint exactly like AppendUtf8Codepoint in acz_hdtex.cpp."""
    if cp == 0:
        return ""
    try:
        return chr(cp)
    except ValueError:
        return ""


def decode_dynamic_gamefont(data: bytes) -> dict:
    """
    Decodes a dynamic tile font (GameFont), computes font and per-glyph hashes (TileHash & ExactHash),
    decodes all UTF-16LE strings, and extracts 4-bit glyph tiles.
    """
    num_strs, num_glyphs, unk06, glyph_dim, cbp, tbp, max_adv = struct.unpack_from("<IHHHHHH", data, 0)
    h_norm, h_clean, h_vis, rdi_off, r10_off, end_idx_off, pal_off = calc_dynamic_font_hashes(data)
    bmp_off = pal_off + 0x40
    tile_bytes_len = (glyph_dim * glyph_dim) // 2
    total_size = bmp_off + num_glyphs * tile_bytes_len
    clut_64 = data[pal_off : pal_off + 64]

    glyphs_meta = []
    for g in range(num_glyphs):
        off = 16 + g * 16
        adv_x, height, runtime_tbp, char_code, flags = struct.unpack_from("<HHIHB", data, off)
        g_off = bmp_off + g * tile_bytes_len
        tile_raw = data[g_off : g_off + tile_bytes_len]
        non_zero = any(b != 0 for b in tile_raw)
        is_non_empty = (char_code > 0x20) and non_zero
        bitmap_tile_hash = calc_glyph_tile_hash(glyph_dim, char_code, adv_x, height, tile_raw, None)
        exact_tile_hash = calc_glyph_tile_hash(glyph_dim, char_code, adv_x, height, tile_raw, clut_64)

        glyphs_meta.append(
            {
                "index": g,
                "char_code": char_code,
                "char": format_cp_utf8(char_code),
                "adv_x": adv_x,
                "height": height,
                "is_non_empty": is_non_empty,
                "bitmap_tile_hash": bitmap_tile_hash,
                "exact_tile_hash": exact_tile_hash,
                "tile_4b": tile_raw,
                "clut_ps2": clut_64,
            }
        )

    rax_off = r10_off + 4 * num_strs
    str_lengths = [struct.unpack_from("<h", data, rdi_off + 2 * s)[0] for s in range(num_strs)]
    str_offsets = [struct.unpack_from("<i", data, r10_off + 4 * s)[0] for s in range(num_strs)]

    strings_decoded = []
    for s in range(num_strs):
        slen = str_lengths[s]
        soff = str_offsets[s]
        if slen <= 0 or soff < 0:
            strings_decoded.append("")
            continue
        chars = []
        for k in range(slen):
            idx_off = rax_off + 2 * (soff + k)
            if idx_off + 2 > end_idx_off:
                break
            g_idx = struct.unpack_from("<h", data, idx_off)[0]
            if g_idx < 0:
                chars.append(" / ")
            elif 0 <= g_idx < num_glyphs:
                chars.append(glyphs_meta[g_idx]["char"])
        strings_decoded.append("".join(chars))

    return {
        "hash_norm": h_norm,
        "hash_clean": h_clean,
        "hash_visual": h_vis,
        "num_strings": num_strs,
        "num_glyphs": num_glyphs,
        "glyph_dim": glyph_dim,
        "total_size": total_size,
        "clut_ps2": clut_64,
        "strings": strings_decoded,
        "glyphs": glyphs_meta,
    }



def clean_restore_and_center_1x_tile(cell_rgba: np.ndarray, cp: int, adv_x: int, height: int, std_h: int = 0) -> np.ndarray:
    """
    Cleans disconnected right-edge and bottom-edge neighbor bleed, restores cut-off left-edge halos (at x < 0),
    and centers the glyph inside the dim x dim cell at pad_x = (dim - adv_x)//2, pad_y = (dim - height)//2.
    """
    dim = cell_rgba.shape[0]
    if cp == 0x20:
        return np.zeros_like(cell_rgba)
    work = cell_rgba.copy()
    a = work[:, :, 3]
    if a.max() == 0:
        return work
    search_start_x = min(dim - 2, max(4, adv_x - 1))
    for x in range(search_start_x, dim):
        if a[:, x].max() == 0:
            work[:, x + 1 :, :] = 0
            break
    if adv_x + 6 < dim:
        work[:, adv_x + 6 :, :] = 0
    a = work[:, :, 3]
    if a.max() == 0:
        return work
    search_start_y = min(dim - 2, max(4, height - 1))
    for y in range(search_start_y, dim):
        if a[y, :].max() == 0:
            work[y + 1 :, :, :] = 0
            break
    if height + 4 < dim:
        work[height + 4 :, :, :] = 0
    a = work[:, :, 3]
    if a.max() == 0:
        return work
    fringe_mask = (a > 0) & (a <= 90)
    has_halo = bool(np.any(fringe_mask))
    max_halo_w = 3 if dim >= 32 else 2
    ext = np.zeros((dim, dim + max_halo_w, 4), dtype=np.uint8)
    ext[:, max_halo_w : max_halo_w + dim] = work
    if has_halo and a[:, 0].max() > 8:
        fringe_pixels = work[fringe_mask]
        halo_rgb = np.median(fringe_pixels[:, :3], axis=0).astype(np.uint8)
        right_halo_vals = []
        for y in range(dim):
            row_a = a[y, :]
            nz_x = np.where(row_a > 120)[0]
            if len(nz_x) > 0:
                rx = int(nz_x.max())
                tail = [int(row_a[rx + d]) for d in range(1, max_halo_w + 1) if rx + d < dim and row_a[rx + d] > 0]
                if tail:
                    right_halo_vals.append(tail)
        if right_halo_vals:
            avg_tail = []
            for d in range(max_halo_w):
                vals = [t[d] for t in right_halo_vals if len(t) > d]
                avg_tail.append(int(round(np.mean(vals))) if vals else 0)
        else:
            avg_tail = [24, 14, 6] if dim >= 32 else [24, 10]
        for y in range(dim):
            a0 = int(a[y, 0])
            if a0 <= 8:
                continue
            if a0 > 120:
                for d in range(1, max_halo_w + 1):
                    ha = avg_tail[d - 1]
                    if ha > 0:
                        ext[y, max_halo_w - d, :3] = halo_rgb
                        ext[y, max_halo_w - d, 3] = ha
            else:
                cur_a = a0
                for d in range(1, max_halo_w + 1):
                    cur_a = int(round(cur_a * 0.55))
                    if cur_a >= 6:
                        ext[y, max_halo_w - d, :3] = halo_rgb
                        ext[y, max_halo_w - d, 3] = cur_a
    ys, xs = np.where(ext[:, :, 3] > 0)
    if len(xs) == 0:
        return np.zeros_like(cell_rgba)
    orig_x0 = int(xs.min()) - max_halo_w
    orig_x1 = int(xs.max()) - max_halo_w + 1
    eff_h = std_h if (0 < std_h <= dim) else height
    pad_x = (dim - min(dim, adv_x)) // 2
    pad_y = (dim - min(dim, eff_h)) // 2
    if orig_x0 + pad_x < 1:
        pad_x = 1 - orig_x0
    if orig_x1 + pad_x > dim - 1:
        pad_x = max(0 - orig_x0, (dim - 1) - orig_x1)
    out = np.zeros((dim, dim, 4), dtype=np.uint8)
    for y in range(dim):
        dy = y + pad_y
        if not (0 <= dy < dim):
            continue
        for x in range(-max_halo_w, dim):
            rgba = ext[y, x + max_halo_w]
            if rgba[3] == 0:
                continue
            dx = x + pad_x
            if 0 <= dx < dim:
                out[dy, dx] = rgba
    return out


def render_glyphs_to_pil_image(
    default_clut_ps2: bytes,
    glyphs: List[dict],
    glyph_dim: int,
    cols: int = 8,
    center_cells: bool = False,
) -> Image.Image:
    """
    Renders a list of 4-bit glyphs into an RGBA PNG atlas with `cols` columns.
    Each glyph uses its own original `clut_ps2` palette (if stored in the glyph dict),
    allowing glyphs from multiple font families to be combined into a single Master Atlas.
    """
    default_pal_raw = np.frombuffer(default_clut_ps2[:64], dtype=np.uint8).reshape(16, 4).copy()
    default_pal_pc = default_pal_raw.copy()
    default_pal_pc[:, 3] = ps2_alpha_to_pc(default_pal_pc[:, 3])

    num_gl = len(glyphs)
    rows = max(1, (num_gl + cols - 1) // cols)
    img_arr = np.zeros((rows * glyph_dim, cols * glyph_dim, 4), dtype=np.uint8)
    tile_bytes = (glyph_dim * glyph_dim) // 2

    pal_cache: Dict[bytes, np.ndarray] = {}

    for g, ge in enumerate(glyphs):
        g_clut = ge.get("clut_ps2")
        if g_clut and len(g_clut) >= 64:
            pal_pc = pal_cache.get(g_clut)
            if pal_pc is None:
                p_raw = np.frombuffer(g_clut[:64], dtype=np.uint8).reshape(16, 4).copy()
                p_raw[:, 3] = ps2_alpha_to_pc(p_raw[:, 3])
                pal_cache[g_clut] = p_raw
                pal_pc = p_raw
        else:
            pal_pc = default_pal_pc

        raw_tile = np.frombuffer(ge["tile_4b"][:tile_bytes], dtype=np.uint8)
        low = raw_tile & 0x0F
        high = (raw_tile >> 4) & 0x0F
        unpacked = np.empty(tile_bytes * 2, dtype=np.uint8)
        unpacked[0::2] = low
        unpacked[1::2] = high
        tile_indices = unpacked.reshape((glyph_dim, glyph_dim))
        gx0 = (g % cols) * glyph_dim
        gy0 = (g // cols) * glyph_dim
        cell_rgba = pal_pc[tile_indices]
        if center_cells:
            cell_rgba = clean_restore_and_center_1x_tile(cell_rgba, ge["char_code"], ge["adv_x"], ge["height"])
        img_arr[gy0 : gy0 + glyph_dim, gx0 : gx0 + glyph_dim] = cell_rgba

    return Image.fromarray(img_arr, mode="RGBA")


def write_master_font_atlas_files(
    fonts_dir: str,
    master_copy_dir: str,
    lang: str,
    glyph_dim: int,
    glyphs: List[dict],
    family_sections: List[dict],
    total_member_blocks: int,
) -> Tuple[str, str]:
    """
    Writes a unified per-language Master Font Atlas for a given tile dimension (e.g. 32x32 or 16x16):
      MASTER_<LANG>_font_<dim>x<dim>.png and MASTER_<LANG>_font_<dim>x<dim>.txt
    Also copies the pair into SCAN_OUT/_MASTER_ATLASES/.
    """
    os.makedirs(fonts_dir, exist_ok=True)
    os.makedirs(master_copy_dir, exist_ok=True)

    num_gl = len(glyphs)
    cols = 16 if num_gl >= 16 else 8
    base_name = f"MASTER_{lang}_font_{glyph_dim}x{glyph_dim}"
    png_path = os.path.join(fonts_dir, base_name + ".png")
    txt_path = os.path.join(fonts_dir, base_name + ".txt")

    default_clut = glyphs[0].get("clut_ps2", b"\x00" * 64) if glyphs else (b"\x00" * 64)
    img = render_glyphs_to_pil_image(default_clut, glyphs, glyph_dim, cols=cols, center_cells=True)
    img.save(png_path, "PNG")

    with open(txt_path, "w", encoding="utf-8", newline="\n") as tf:
        tf.write("====================================================================\n")
        tf.write(
            f" Master Font Atlas [{lang}] — All {glyph_dim}x{glyph_dim} Language Glyphs Combined ({SCANNER_VERSION})\n"
        )
        tf.write("====================================================================\n")
        tf.write(f"Master Atlas Name:     {base_name}\n")
        tf.write(f"Language:              {lang}\n")
        tf.write(f"Glyph Dimension:       {glyph_dim}x{glyph_dim}\n")
        tf.write(f"Grid Columns:          {cols}\n")
        tf.write("CenteredCells:         1\n")
        tf.write(f"Collected Glyphs:      {num_gl} ({glyph_dim}x{glyph_dim}, {cols} columns)\n")
        tf.write(f"Member Font Blocks:    {total_member_blocks}\n")
        tf.write("To replace all fonts of this size in-game:\n")
        tf.write(f"  Copy edited {base_name}.png AND {base_name}.txt into mods\\textures\\\n")
        tf.write("--------------------------------------------------------------------\n")
        tf.write("TYPEFACE SECTIONS IN THIS MASTER ATLAS:\n")
        for sec in family_sections:
            tf.write(
                f"  * Glyphs #{sec['start_idx']:03d}..#{sec['end_idx']:03d} "
                f"(rows {sec['start_idx'] // cols}..{sec['end_idx'] // cols}): "
                f"Family={sec['family_hash']:016X} ({sec['new_glyphs']} unique glyphs, "
                f"{sec['member_blocks']} blocks) | Sample: \"{sec['sample']}\"\n"
            )
        tf.write("--------------------------------------------------------------------\n")
        tf.write(f"GLYPH METRICS (Grid {cols} columns):\n")
        for g_idx, ge in enumerate(glyphs):
            alts = sorted(
                h for h in ge.get("alt_hashes", set())
                if h not in (ge["bitmap_tile_hash"], ge["exact_tile_hash"])
            )
            alt_str = (" | AltHashes=" + ",".join(f"{h:016X}" for h in alts)) if alts else ""
            tf.write(
                f"  Glyph #{g_idx:03d} (col={g_idx % cols}, row={g_idx // cols}): "
                f"U+{ge['char_code']:04X} '{ge['char']}' | "
                f"adv_x={ge['adv_x']}, height={ge['height']} | "
                f"TileHash={ge['bitmap_tile_hash']:016X} | "
                f"ExactHash={ge['exact_tile_hash']:016X} | "
                f"Family={ge.get('family_hash', 0):016X}{alt_str}\n"
            )

    shutil.copy2(png_path, os.path.join(master_copy_dir, base_name + ".png"))
    shutil.copy2(txt_path, os.path.join(master_copy_dir, base_name + ".txt"))
    return base_name + ".png", base_name + ".txt"


def write_master_16x16_gim_atlas(
    fonts_dir: str,
    master_copy_dir: str,
    lang: str,
    gim_items: List[Tuple[str, Image.Image, dict, List[str]]],
) -> Optional[Tuple[str, str]]:
    """
    Combines all 16x16 GIM textures for a language into a single Master Atlas:
      MASTER_<LANG>_gim_16x16.png and MASTER_<LANG>_gim_16x16.txt
    When copied into mods/textures/, acz_hdtex.dll automatically slices and replaces
    all matching 16x16 GIM textures by their GimHash.
    """
    if not gim_items:
        return None

    os.makedirs(fonts_dir, exist_ok=True)
    os.makedirs(master_copy_dir, exist_ok=True)

    num_items = len(gim_items)
    cols = 16 if num_items >= 16 else 8
    rows = max(1, (num_items + cols - 1) // cols)
    dim = 16

    canvas = Image.new("RGBA", (cols * dim, rows * dim), (0, 0, 0, 0))
    for idx, (_, img, _, _) in enumerate(gim_items):
        gx0 = (idx % cols) * dim
        gy0 = (idx // cols) * dim
        canvas.paste(img.convert("RGBA"), (gx0, gy0))

    base_name = f"MASTER_{lang}_gim_16x16"
    png_path = os.path.join(fonts_dir, base_name + ".png")
    txt_path = os.path.join(fonts_dir, base_name + ".txt")
    canvas.save(png_path, "PNG")

    with open(txt_path, "w", encoding="utf-8", newline="\n") as tf:
        tf.write("====================================================================\n")
        tf.write(f" Master 16x16 GIM Atlas [{lang}] — All 16x16 GIM Textures Combined ({SCANNER_VERSION})\n")
        tf.write("====================================================================\n")
        tf.write(f"Master Atlas Name:     {base_name}\n")
        tf.write(f"Language:              {lang}\n")
        tf.write("Glyph Dimension:       16x16\n")
        tf.write(f"Grid Columns:          {cols}\n")
        tf.write(f"Collected Glyphs:      {num_items} (16x16, {cols} columns)\n")
        tf.write("To replace all 16x16 GIM textures in-game:\n")
        tf.write(f"  Copy edited {base_name}.png AND {base_name}.txt into mods\\textures\\\n")
        tf.write("--------------------------------------------------------------------\n")
        tf.write(f"GIM TILE METRICS (Grid {cols} columns):\n")
        for idx, (h_hex, _, meta, sources) in enumerate(gim_items):
            src0 = sources[0] if sources else ""
            tf.write(
                f"  GimTile #{idx:03d} (col={idx % cols}, row={idx // cols}): "
                f"GimHash={h_hex} | GimExactHash={meta['hash_exact']} | "
                f"GimFhmHash={meta['hash_fhm_norm']} | PSM=0x{meta['psm']:02X} | Source={src0}\n"
            )

    shutil.copy2(png_path, os.path.join(master_copy_dir, base_name + ".png"))
    shutil.copy2(txt_path, os.path.join(master_copy_dir, base_name + ".txt"))
    return base_name + ".png", base_name + ".txt"


def write_individual_font_files(
    fonts_dir: str,
    finfo: dict,
    family_hash: int,
    lang: str,
    all_langs_for_hash: Set[str],
    sources: List[str],
) -> Tuple[str, str]:
    """Writes <HASH>_font_<N>gl_<dim>x<dim>.png and .txt (when --dump-individuals is enabled)."""
    os.makedirs(fonts_dir, exist_ok=True)
    h_norm = finfo["hash_norm"]
    num_gl = finfo["num_glyphs"]
    dim = finfo["glyph_dim"]
    base_name = f"{h_norm:016X}_font_{num_gl}gl_{dim}x{dim}"
    png_path = os.path.join(fonts_dir, base_name + ".png")
    txt_path = os.path.join(fonts_dir, base_name + ".txt")

    img = render_glyphs_to_pil_image(finfo["clut_ps2"], finfo["glyphs"], dim, cols=8)
    img.save(png_path, "PNG")

    langs_str = ", ".join(sorted(all_langs_for_hash)) if all_langs_for_hash else lang
    with open(txt_path, "w", encoding="utf-8", newline="\n") as tf:
        tf.write("====================================================================\n")
        tf.write(" Dynamic Font Atlas Dump (GameFont / CUnicodeFont / Pmenu)\n")
        tf.write("====================================================================\n")
        tf.write(f"Primary Hash (norm):   {h_norm:016X}\n")
        tf.write(f"Font Family Hash:      {family_hash:016X}\n")
        tf.write(f"Clean Hash (no pad):   {finfo['hash_clean']:016X}\n")
        tf.write(f"Visual Atlas Hash:     {finfo['hash_visual']:016X}\n")
        tf.write(f"Language Folder:       {lang} (Used in: {langs_str})\n")
        tf.write("Grid Columns:          8\n")
        tf.write(f"Glyphs:                {num_gl} ({dim}x{dim}, 8 columns)\n")
        tf.write(f"Strings:               {finfo['num_strings']}\n")
        tf.write(f"To replace in-game:    Copy edited PNG to mods\\textures\\{h_norm:016X}.png\n")
        if sources:
            tf.write("Source Files in Archive:\n")
            for src in sources[:25]:
                tf.write(f"  - {src}\n")
            if len(sources) > 25:
                tf.write(f"  ... and {len(sources) - 25} more locations\n")
        tf.write("--------------------------------------------------------------------\n")
        tf.write("DECODED STRINGS:\n")
        for s_idx, text in enumerate(finfo["strings"]):
            tf.write(f"  [{s_idx:03d}] {text}\n")
        tf.write("--------------------------------------------------------------------\n")
        tf.write("GLYPH METRICS (Grid 8 columns):\n")
        for g_idx, ge in enumerate(finfo["glyphs"]):
            tf.write(
                f"  Glyph #{g_idx:03d} (col={g_idx % 8}, row={g_idx // 8}): "
                f"U+{ge['char_code']:04X} '{ge['char']}' | "
                f"adv_x={ge['adv_x']}, height={ge['height']} | "
                f"TileHash={ge['bitmap_tile_hash']:016X} | "
                f"ExactHash={ge['exact_tile_hash']:016X}\n"
            )

    return base_name + ".png", base_name + ".txt"


def write_family_font_files(fonts_dir: str, fam: dict, lang: str) -> Tuple[str, str]:
    """Writes merged typeface atlas FAMILY_<HASH>_font_<dim>x<dim>.png and .txt (when --dump-families is enabled)."""
    os.makedirs(fonts_dir, exist_ok=True)
    fhash = fam["family_hash"]
    dim = fam["glyph_dim"]
    glyphs = fam["glyphs"]
    num_gl = len(glyphs)
    base_name = f"FAMILY_{fhash:016X}_font_{dim}x{dim}"
    png_path = os.path.join(fonts_dir, base_name + ".png")
    txt_path = os.path.join(fonts_dir, base_name + ".txt")

    img = render_glyphs_to_pil_image(fam["clut_ps2"], glyphs, dim, cols=8)
    img.save(png_path, "PNG")

    with open(txt_path, "w", encoding="utf-8", newline="\n") as tf:
        tf.write("====================================================================\n")
        tf.write(
            f" Merged Font Family Atlas [{lang}] (Auto-Combined across {len(fam['member_hashes'])} Dialogue/Menu Blocks)\n"
        )
        tf.write("====================================================================\n")
        tf.write(f"Family Hash:           {fhash:016X}\n")
        tf.write(f"Language:              {lang}\n")
        tf.write("Grid Columns:          8\n")
        tf.write(f"Collected Glyphs:      {num_gl} ({dim}x{dim}, 8 columns)\n")
        tf.write(f"Member Font Blocks:    {len(fam['member_hashes'])}\n")
        tf.write("To replace entire family in-game:\n")
        tf.write(f"  Copy edited {base_name}.png (and this .txt file) into mods\\textures\\\n")
        if fam.get("sources"):
            tf.write("Sample Archive Locations:\n")
            for src in fam["sources"][:20]:
                tf.write(f"  - {src}\n")
            if len(fam["sources"]) > 20:
                tf.write(f"  ... and {len(fam['sources']) - 20} more archive entries\n")
        tf.write("--------------------------------------------------------------------\n")
        tf.write("SAMPLE STRINGS FROM THIS FONT FAMILY:\n")
        for i, (mhash, s_text) in enumerate(zip(fam["member_hashes"], fam["sample_strings"])):
            tf.write(f"  [{i:03d}] (Hash={mhash:016X}) {s_text}\n")
        tf.write("--------------------------------------------------------------------\n")
        tf.write("GLYPH METRICS (Grid 8 columns):\n")
        for g_idx, ge in enumerate(glyphs):
            tf.write(
                f"  Glyph #{g_idx:03d} (col={g_idx % 8}, row={g_idx // 8}): "
                f"U+{ge['char_code']:04X} '{ge['char']}' | "
                f"adv_x={ge['adv_x']}, height={ge['height']} | "
                f"TileHash={ge['bitmap_tile_hash']:016X} | "
                f"ExactHash={ge['exact_tile_hash']:016X}\n"
            )

    return base_name + ".png", base_name + ".txt"


def parse_fhm_header(data: bytes) -> Optional[Tuple[int, List[Tuple[int, int, int]]]]:
    """
    Checks whether `data` is an .FHM container.
    Supports:
      - Standard FHM containers (with end_marker 0 or len(data)).
      - Containers with count % 4 == 3 (including 7-slot language FHMs where 4*(count+1) is already
        a multiple of 16 and the first child starts immediately at 4*(count+1) without an end_marker word).
      - Containers with empty slots (offs[i] == 0 or offs[i] == offs[i+1]).
    Returns (count, [(slot_idx, start_off, end_off), ...]) or None.
    """
    data_len = len(data)
    if data_len < 32 or data[:4] == GIM_MAGIC:
        return None
    if try_parse_gamefont_at(data, 0) > 0:
        return None

    count = struct.unpack_from("<I", data, 0)[0]
    if count == 0 or count > 2048 or 4 * (count + 1) > data_len:
        return None

    offs = [struct.unpack_from("<I", data, 4 + 4 * i)[0] for i in range(count)]
    non_zero_offs = [o for o in offs if o != 0]
    if not non_zero_offs:
        return None

    first_nz = non_zero_offs[0]
    table_bytes = 4 * (count + 1)
    min_first = (table_bytes + 15) & ~15

    if first_nz < table_bytes or first_nz > min_first + 32 or (first_nz & 0xF) != 0:
        return None

    # If there is at least 4 bytes of padding between the offset table and the first child, check end_marker
    if table_bytes + 4 <= first_nz:
        end_marker = struct.unpack_from("<I", data, table_bytes)[0]
        if end_marker not in (0, data_len):
            return None

    # Reject caption tables (01 00 00 00 ...) where count == 1 and offset is 0x80+
    if count == 1 and first_nz > 0x20:
        return None

    prev_nz = 0
    for o in offs:
        if o == 0:
            continue
        if (o & 0xF) != 0 or o >= data_len or o < prev_nz:
            return None
        prev_nz = o

    spans: List[Tuple[int, int, int]] = []
    for i in range(count):
        start = offs[i]
        if start == 0:
            continue
        end = data_len
        for j in range(i + 1, count):
            if offs[j] != 0:
                end = offs[j]
                break
        if end > start:
            spans.append((i, start, end))

    if not spans:
        return None

    return count, spans


def is_font_related_path(rel_path: str) -> bool:
    """Checks whether a file path or name is related to fonts, menus, subtitles, or briefings."""
    low = rel_path.lower().replace("\\", "/")
    return any(k in low for k in FONT_KEYWORDS)


class LangBucket:
    """Stores dynamic fonts and GIM textures belonging to a specific localization language."""

    def __init__(self, lang: str) -> None:
        self.lang = lang
        self.dynamic_fonts: Dict[int, dict] = {}
        self.font_sources: Dict[int, List[str]] = {}
        self.font_gims: Dict[str, Tuple[Image.Image, dict, List[str]]] = {}
        self.gim_16x16: Dict[str, Tuple[Image.Image, dict, List[str]]] = {}
        self.other_gims: Dict[str, Tuple[Image.Image, dict, List[str]]] = {}
        self.native_font_pngs: List[Tuple[str, str]] = []


class FontScannerState:
    def __init__(
        self,
        include_subtitles: bool,
        all_textures: bool,
        dump_families: bool = False,
        dump_individuals: bool = False,
        allowed_langs: Optional[Set[str]] = None,
    ) -> None:
        self.include_subtitles = include_subtitles
        self.all_textures = all_textures
        self.dump_families = dump_families
        self.dump_individuals = dump_individuals
        self.allowed_langs = allowed_langs
        self.buckets: Dict[str, LangBucket] = {}
        self.global_decoded_fonts: Dict[int, dict] = {}
        self.hash_to_all_langs: Dict[int, Set[str]] = {}

    def get_bucket(self, lang: str) -> LangBucket:
        lang = normalize_lang_code(lang)
        b = self.buckets.get(lang)
        if b is None:
            b = LangBucket(lang)
            self.buckets[lang] = b
        return b

    def is_lang_allowed(self, lang: str) -> bool:
        if not self.allowed_langs:
            return True
        norm = normalize_lang_code(lang)
        return norm in self.allowed_langs or norm == "COMMON"


def register_found_gamefont(
    finfo: dict,
    rel_source_path: str,
    loc_tag: str,
    state: FontScannerState,
    explicit_lang: Optional[str],
    inferred_container_lang: Optional[str] = None,
) -> str:
    """Registers a decoded GameFont block in the appropriate language bucket."""
    h_norm = finfo["hash_norm"]
    if h_norm not in state.global_decoded_fonts:
        state.global_decoded_fonts[h_norm] = finfo
    else:
        finfo = state.global_decoded_fonts[h_norm]

    lang = explicit_lang
    if not lang:
        content_lang = detect_lang_from_font_content(finfo)
        if content_lang != "COMMON":
            lang = content_lang
        elif inferred_container_lang:
            lang = inferred_container_lang
        else:
            lang = "COMMON"
    lang = normalize_lang_code(lang)

    if h_norm not in state.hash_to_all_langs:
        state.hash_to_all_langs[h_norm] = set()
    state.hash_to_all_langs[h_norm].add(lang)

    if state.is_lang_allowed(lang):
        src_desc = f"{rel_source_path} [{loc_tag}]"
        bucket = state.get_bucket(lang)
        if h_norm not in bucket.dynamic_fonts:
            bucket.dynamic_fonts[h_norm] = finfo
            bucket.font_sources[h_norm] = [src_desc]
        else:
            if src_desc not in bucket.font_sources[h_norm]:
                bucket.font_sources[h_norm].append(src_desc)

    return lang


def register_found_gim(
    gim_bytes: bytes,
    rel_source_path: str,
    loc_tag: str,
    state: FontScannerState,
    explicit_lang: Optional[str],
    sibling_has_font: bool,
) -> None:
    """Registers a decoded GIM\\0 texture in the appropriate language bucket."""
    if len(gim_bytes) < 0x30 or gim_bytes[:4] != GIM_MAGIC:
        return
    w, h = struct.unpack_from("<HH", gim_bytes, 0x1C)
    is_16x16_gim = w == 16 and h == 16
    is_font_gim = is_font_related_path(rel_source_path) or sibling_has_font
    if not is_font_gim and not is_16x16_gim and not state.all_textures:
        return

    lang = normalize_lang_code(explicit_lang or "COMMON")
    if not state.is_lang_allowed(lang):
        return

    try:
        img, meta = decode_gim_bytes(gim_bytes)
        h_hex = meta["hash"]
        src_desc = f"{rel_source_path} [{loc_tag}]"
        bucket = state.get_bucket(lang)
        if is_16x16_gim:
            if h_hex not in bucket.gim_16x16:
                bucket.gim_16x16[h_hex] = (img, meta, [src_desc])
            else:
                bucket.gim_16x16[h_hex][2].append(src_desc)
        if is_font_gim:
            if h_hex not in bucket.font_gims:
                bucket.font_gims[h_hex] = (img, meta, [src_desc])
            else:
                bucket.font_gims[h_hex][2].append(src_desc)
        elif state.all_textures and not is_16x16_gim:
            if h_hex not in bucket.other_gims:
                bucket.other_gims[h_hex] = (img, meta, [src_desc])
            else:
                bucket.other_gims[h_hex][2].append(src_desc)
    except Exception:
        pass


def scan_raw_stream_for_resources(
    data: bytes,
    rel_source_path: str,
    state: FontScannerState,
    path_lang: Optional[str] = None,
    prefix: str = "",
    sibling_has_font: bool = False,
    inherited_slot_lang: Optional[str] = None,
) -> int:
    """
    Scans an arbitrary binary buffer (FHM leaf node, .MSA, .BFN, .BIN, .DAT, .PAC, etc.)
    in 16-byte steps and extracts all concatenated or embedded GameFont blocks and GIM\\0 textures.
    When multiple GameFont blocks exist in the same stream (e.g. 6 fonts in select.msa or caption tables),
    blocks without an explicit language inherit the dominant language of sibling blocks in that stream.
    Returns the number of GameFont blocks found.
    """
    data_len = len(data)
    if data_len < 0x30:
        return 0

    explicit_lang = path_lang or inherited_slot_lang
    base_tag = prefix.rstrip("_") or "root"

    found_fonts: List[Tuple[int, dict]] = []
    found_gims: List[Tuple[int, int]] = []

    off = 0
    while off <= data_len - 0x30:
        # 1. Check for a dynamic GameFont block at `off`
        if off + 0x60 <= data_len and data[off : off + 4] != GIM_MAGIC:
            gf_size = try_parse_gamefont_at(data, off)
            if gf_size > 0:
                try:
                    finfo = decode_dynamic_gamefont(data[off : off + gf_size])
                    found_fonts.append((off, finfo))
                    off += (gf_size + 15) & ~15
                    continue
                except Exception:
                    pass

        # 2. Check for a GIM\0 texture at `off`
        if data[off : off + 4] == GIM_MAGIC:
            gim_size = try_parse_gim_at(data, off)
            if gim_size > 0:
                found_gims.append((off, gim_size))
                off += (gim_size + 15) & ~15
                continue

        off += 16

    # If no explicit language was given by path or 7-slot FHM, infer dominant stream language
    inferred_stream_lang: Optional[str] = None
    if not explicit_lang and found_fonts:
        lang_counts: Dict[str, int] = {}
        for _, finfo in found_fonts:
            detected = detect_lang_from_font_content(finfo)
            if detected != "COMMON":
                lang_counts[detected] = lang_counts.get(detected, 0) + 1
        if lang_counts:
            inferred_stream_lang = max(lang_counts.items(), key=lambda kv: kv[1])[0]

    has_any_font = sibling_has_font or (len(found_fonts) > 0)

    for _, (f_off, finfo) in enumerate(found_fonts):
        if f_off == 0 and len(found_fonts) == 1:
            loc_tag = f"{base_tag}_{explicit_lang}" if (base_tag != "root" and inherited_slot_lang) else base_tag
        else:
            loc_tag = f"{base_tag}+0x{f_off:X}"
        register_found_gamefont(
            finfo=finfo,
            rel_source_path=rel_source_path,
            loc_tag=loc_tag,
            state=state,
            explicit_lang=explicit_lang,
            inferred_container_lang=inferred_stream_lang,
        )

    eff_gim_lang = explicit_lang or inferred_stream_lang
    for g_off, g_size in found_gims:
        loc_tag = base_tag if (g_off == 0 and len(found_gims) == 1) else f"{base_tag}+0x{g_off:X}"
        register_found_gim(
            gim_bytes=data[g_off : g_off + g_size],
            rel_source_path=rel_source_path,
            loc_tag=loc_tag,
            state=state,
            explicit_lang=eff_gim_lang,
            sibling_has_font=has_any_font,
        )

    return len(found_fonts)


def collect_from_node(
    data: bytes,
    rel_source_path: str,
    state: FontScannerState,
    path_lang: Optional[str] = None,
    prefix: str = "",
    parent_count: int = 1,
    entry_index: int = 0,
    sibling_has_font: bool = False,
    inherited_slot_lang: Optional[str] = None,
) -> int:
    """
    Recursively traverses an .FHM container (or arbitrary binary file .MSA / .BFN / .BIN / .DAT / .PAC)
    and distributes all discovered GameFont blocks and GIM textures into language buckets.
    Returns the number of GameFont blocks found in this node.
    """
    fhm = parse_fhm_header(data)
    if fhm is not None:
        count, spans = fhm
        total_fonts_in_fhm = 0
        for slot_i, start, end in spans:
            sub_data = data[start:end]
            sub_prefix = f"{prefix}g{slot_i:02d}_" if prefix == "" else f"{prefix}e{slot_i:02d}_"
            slot_lang = PMENU_LANG_TAGS.get(slot_i) if count == 7 else inherited_slot_lang
            total_fonts_in_fhm += collect_from_node(
                sub_data,
                rel_source_path,
                state,
                path_lang=path_lang,
                prefix=sub_prefix,
                parent_count=count,
                entry_index=slot_i,
                sibling_has_font=sibling_has_font,
                inherited_slot_lang=slot_lang,
            )
        return total_fonts_in_fhm

    return scan_raw_stream_for_resources(
        data=data,
        rel_source_path=rel_source_path,
        state=state,
        path_lang=path_lang,
        prefix=prefix,
        sibling_has_font=sibling_has_font,
        inherited_slot_lang=inherited_slot_lang,
    )


def find_matching_family(finfo: dict, families: Dict[int, dict]) -> int:
    """
    Exact equivalent of FindMatchingFontFamily in acz_hdtex.cpp:
    A font belongs to a family if its glyph_dim matches and >= 75% of shared
    non-empty character codes have a 100% identical 4-bit tile and metrics (bitmap_tile_hash).
    """
    best_family = 0
    best_exact = 0
    dim = finfo["glyph_dim"]

    for fhash, fam in families.items():
        if fam["glyph_dim"] != dim or not fam["glyphs"]:
            continue

        common_chars = 0
        exact_tiles = 0
        fam_char_to_tiles: Dict[int, Set[int]] = fam["char_to_tiles"]

        for rec in finfo["glyphs"]:
            if not rec["is_non_empty"]:
                continue
            cp = rec["char_code"]
            if cp in fam_char_to_tiles:
                common_chars += 1
                if rec["bitmap_tile_hash"] in fam_char_to_tiles[cp]:
                    exact_tiles += 1

        if exact_tiles >= 1 and common_chars > 0 and (exact_tiles * 4 >= common_chars * 3):
            if exact_tiles > best_exact:
                best_exact = exact_tiles
                best_family = fhash

    return best_family



def decode_glyph_rgba(ge: dict, dim: int, default_clut: bytes) -> np.ndarray:
    if "_rgba" in ge:
        return ge["_rgba"]
    clut = ge.get("clut_ps2") or default_clut
    p_raw = np.frombuffer(clut[:64], dtype=np.uint8).reshape(16, 4).copy()
    p_raw[:, 3] = ps2_alpha_to_pc(p_raw[:, 3])
    tile_bytes = (dim * dim) // 2
    raw_tile = np.frombuffer(ge["tile_4b"][:tile_bytes], dtype=np.uint8)
    unpacked = np.empty(tile_bytes * 2, dtype=np.uint8)
    unpacked[0::2] = raw_tile & 0x0F
    unpacked[1::2] = (raw_tile >> 4) & 0x0F
    rgba = p_raw[unpacked.reshape((dim, dim))].astype(np.int16)
    ge["_rgba"] = rgba
    return rgba


def are_glyphs_visually_identical(ga: dict, gb: dict, dim: int, cl_a: bytes, cl_b: bytes) -> bool:
    if ga["bitmap_tile_hash"] == gb["bitmap_tile_hash"]:
        return True
    if abs(ga["adv_x"] - gb["adv_x"]) > 1 or abs(ga["height"] - gb["height"]) > 1:
        return False
    ra = decode_glyph_rgba(ga, dim, cl_a)
    rb = decode_glyph_rgba(gb, dim, cl_b)
    return int(np.max(np.abs(ra - rb))) <= 32


def consolidate_overlapping_families(
    families: Dict[int, dict],
    font_to_family: Dict[int, int],
) -> None:
    """
    Performs transitive merging of font families of the same `glyph_dim` whenever two families
    share >= 75% identical non-empty glyphs (bitmap_tile_hash) on common character codes after
    intermediate bridging blocks are added. Ensures 1-line subtitles and squadron names merge
    into complete, unified typefaces.
    """
    merged_any = True
    while merged_any:
        merged_any = False
        fam_list = sorted(
            families.values(),
            key=lambda fm: (-len(fm["glyphs"]), -len(fm["member_hashes"]), fm["family_hash"]),
        )
        n = len(fam_list)
        for i in range(n):
            fam_a = fam_list[i]
            hash_a = fam_a["family_hash"]
            if hash_a not in families:
                continue
            for j in range(i + 1, n):
                fam_b = fam_list[j]
                hash_b = fam_b["family_hash"]
                if hash_b not in families or fam_a["glyph_dim"] != fam_b["glyph_dim"]:
                    continue

                common_chars = 0
                exact_tiles = 0
                dim = fam_a["glyph_dim"]
                a_by_cp: Dict[int, List[dict]] = {}
                for ga in fam_a["glyphs"]:
                    a_by_cp.setdefault(ga["char_code"], []).append(ga)

                for ge_b in fam_b["glyphs"]:
                    if not ge_b.get("is_non_empty", False):
                        continue
                    cp = ge_b["char_code"]
                    if cp in a_by_cp:
                        common_chars += 1
                        if any(are_glyphs_visually_identical(ga, ge_b, dim, fam_a["clut_ps2"], fam_b["clut_ps2"]) for ga in a_by_cp[cp]):
                            exact_tiles += 1

                if exact_tiles >= 1 and common_chars > 0 and (exact_tiles * 4 >= common_chars * 3):
                    # Merge fam_b into the larger family fam_a
                    for mh in fam_b["member_hashes"]:
                        if mh not in fam_a["member_hashes"]:
                            fam_a["member_hashes"].append(mh)
                        font_to_family[mh] = hash_a
                    for ss in fam_b["sample_strings"]:
                        if len(fam_a["sample_strings"]) < 64 and ss not in fam_a["sample_strings"]:
                            fam_a["sample_strings"].append(ss)
                    for src in fam_b["sources"]:
                        if src not in fam_a["sources"]:
                            fam_a["sources"].append(src)
                    for ge_b in fam_b["glyphs"]:
                        cp = ge_b["char_code"]
                        matched_ga = None
                        for ga in a_by_cp.get(cp, []):
                            if are_glyphs_visually_identical(ga, ge_b, dim, fam_a["clut_ps2"], fam_b["clut_ps2"]):
                                matched_ga = ga
                                break
                        if matched_ga is not None:
                            alts = matched_ga.setdefault("alt_hashes", set())
                            alts.add(ge_b["bitmap_tile_hash"])
                            alts.add(ge_b["exact_tile_hash"])
                            for ah in ge_b.get("alt_hashes", set()):
                                alts.add(ah)
                            fam_a["seen_tile_hashes"].add(ge_b["bitmap_tile_hash"])
                            fam_a["char_to_tiles"].setdefault(cp, set()).add(ge_b["bitmap_tile_hash"])
                        else:
                            th = ge_b["bitmap_tile_hash"]
                            fam_a["seen_tile_hashes"].add(th)
                            ge_copy = dict(ge_b)
                            ge_copy["family_hash"] = hash_a
                            ge_copy["alt_hashes"] = set(ge_b.get("alt_hashes", set()))
                            fam_a["glyphs"].append(ge_copy)
                            a_by_cp.setdefault(cp, []).append(ge_copy)
                            fam_a["char_to_tiles"].setdefault(cp, set()).add(th)

                    del families[hash_b]
                    merged_any = True
                    break
            if merged_any:
                break


def build_master_atlases_and_dump(state: FontScannerState, scan_out_root: str) -> None:
    """
    For each localization language (EN, DE, FR, ES, IT, JP, RU, COMMON), builds unified
    Master Atlases for each tile dimension (separately for 32x32, 16x16, etc.),
    plus a dedicated Master Atlas for 16x16 GIM textures.
    """
    os.makedirs(scan_out_root, exist_ok=True)
    master_copy_dir = os.path.join(scan_out_root, "_MASTER_ATLASES")
    os.makedirs(master_copy_dir, exist_ok=True)

    # If only untagged Latin blocks (placed in COMMON) were found and EN does not exist yet,
    # create the EN bucket so the user always gets ready-to-use MASTER_EN_font_32x32.png / 16x16.png
    if "COMMON" in state.buckets and "EN" not in state.buckets:
        if state.is_lang_allowed("EN"):
            state.get_bucket("EN")

    master_catalog_lines: List[str] = []

    total_unique_fonts_global = len(state.global_decoded_fonts)
    total_master_font_atlases = 0
    total_master_gim16_atlases = 0
    total_dumped_families = 0
    total_dumped_individuals = 0
    total_dumped_font_gims = 0
    total_dumped_other_gims = 0

    def _lang_sort_key(l_code: str) -> Tuple[int, str]:
        idx = LANG_DISPLAY_ORDER.index(l_code) if l_code in LANG_DISPLAY_ORDER else 999
        return (idx, l_code)

    common_bucket = state.buckets.get("COMMON")
    sorted_langs = sorted(state.buckets.keys(), key=_lang_sort_key)

    for lang in sorted_langs:
        if state.allowed_langs and lang not in state.allowed_langs:
            continue

        bucket = state.buckets[lang]
        # Combine language-specific dynamic fonts with shared COMMON fonts so that a single
        # language Master Atlas (e.g. MASTER_EN_font_32x32.png) covers 100% of glyphs!
        combined_fonts: Dict[int, dict] = dict(bucket.dynamic_fonts)
        combined_sources: Dict[int, List[str]] = {k: list(v) for k, v in bucket.font_sources.items()}
        if lang != "COMMON" and common_bucket is not None:
            for h_norm, cf in common_bucket.dynamic_fonts.items():
                if h_norm not in combined_fonts:
                    combined_fonts[h_norm] = cf
                    combined_sources[h_norm] = list(common_bucket.font_sources.get(h_norm, []))

        combined_gim16: Dict[str, Tuple[Image.Image, dict, List[str]]] = dict(bucket.gim_16x16)
        if lang != "COMMON" and common_bucket is not None:
            for h_hex, g_item in common_bucket.gim_16x16.items():
                if h_hex not in combined_gim16:
                    combined_gim16[h_hex] = g_item

        if (
            not combined_fonts
            and not combined_gim16
            and not bucket.font_gims
            and not bucket.native_font_pngs
            and not (state.all_textures and bucket.other_gims)
        ):
            continue

        lang_root = os.path.join(scan_out_root, lang)
        fonts_dir = os.path.join(lang_root, "fonts")
        fonts_gim_dir = os.path.join(lang_root, "fonts_gim")
        all_gim_dir = os.path.join(lang_root, "textures_gim")
        os.makedirs(lang_root, exist_ok=True)

        lang_catalog_lines: List[str] = []
        master_catalog_lines.append(
            f"============================== LOCALIZATION LANGUAGE: {lang} =============================="
        )

        sorted_fonts = sorted(
            combined_fonts.values(),
            key=lambda f: (
                -f["num_glyphs"],
                -f["num_strings"],
                f["hash_norm"],
            ),
        )

        families: Dict[int, dict] = {}
        font_to_family: Dict[int, int] = {}
        font_added_new_glyphs: Dict[int, int] = {}

        for finfo in sorted_fonts:
            h_norm = finfo["hash_norm"]
            fam_hash = find_matching_family(finfo, families)
            if fam_hash == 0:
                fam_hash = h_norm
                families[fam_hash] = {
                    "family_hash": fam_hash,
                    "glyph_dim": finfo["glyph_dim"],
                    "clut_ps2": finfo["clut_ps2"],
                    "glyphs": [],
                    "seen_tile_hashes": set(),
                    "char_to_tiles": {},
                    "member_hashes": [],
                    "sample_strings": [],
                    "sources": [],
                }

            font_to_family[h_norm] = fam_hash
            fam = families[fam_hash]
            fam["member_hashes"].append(h_norm)
            first_str = finfo["strings"][0] if finfo["strings"] else ""
            if len(first_str) > 90:
                first_str = first_str[:90] + "..."
            fam["sample_strings"].append(first_str)

            for src in combined_sources.get(h_norm, []):
                if src not in fam["sources"]:
                    fam["sources"].append(src)

            added = 0
            for ge in finfo["glyphs"]:
                th = ge["bitmap_tile_hash"]
                eh = ge["exact_tile_hash"]
                cp = ge["char_code"]
                matched_existing = None
                for ex_ge in fam["glyphs"]:
                    if ex_ge["char_code"] == cp and are_glyphs_visually_identical(ex_ge, ge, finfo["glyph_dim"], fam["clut_ps2"], finfo["clut_ps2"]):
                        matched_existing = ex_ge
                        break
                if matched_existing is not None:
                    alts = matched_existing.setdefault("alt_hashes", set())
                    alts.add(th)
                    alts.add(eh)
                    fam["seen_tile_hashes"].add(th)
                    fam["char_to_tiles"].setdefault(cp, set()).add(th)
                elif th not in fam["seen_tile_hashes"]:
                    fam["seen_tile_hashes"].add(th)
                    ge_copy = dict(ge)
                    ge_copy["family_hash"] = fam_hash
                    ge_copy["alt_hashes"] = set()
                    fam["glyphs"].append(ge_copy)
                    fam["char_to_tiles"].setdefault(cp, set()).add(th)
                    added += 1

            font_added_new_glyphs[h_norm] = added

        # Transitively merge families connected via intermediate blocks
        consolidate_overlapping_families(families, font_to_family)

        # Sort glyphs within each family by Unicode character code (U+0020 .. U+FFFF)
        for fam in families.values():
            fam["glyphs"].sort(key=lambda g: (g["char_code"], g["exact_tile_hash"], g["bitmap_tile_hash"]))

        # Group families by glyph_dim (32x32, 16x16, etc.) to build per-dimension Master Atlases
        families_by_dim: Dict[int, List[dict]] = {}
        for fam in families.values():
            dim = fam["glyph_dim"]
            families_by_dim.setdefault(dim, []).append(fam)

        for dim, dim_fams in sorted(families_by_dim.items(), key=lambda kv: (-kv[0])):
            # Sort typefaces so major menu/dialogue fonts appear first
            dim_fams.sort(key=lambda fm: (-len(fm["glyphs"]), -len(fm["member_hashes"]), fm["family_hash"]))

            master_glyphs: List[dict] = []
            seen_master_exact_tiles: Set[int] = set()
            family_sections: List[dict] = []
            total_blocks_dim = 0

            for fm in dim_fams:
                total_blocks_dim += len(fm["member_hashes"])
                sec_start = len(master_glyphs)
                added_in_sec = 0
                for ge in fm["glyphs"]:
                    # Deduplicate across typefaces by exact_tile_hash (including CLUT palette)
                    # so shared uncolored shapes across different fonts are preserved in each typeface section.
                    eh = ge["exact_tile_hash"]
                    if eh not in seen_master_exact_tiles:
                        seen_master_exact_tiles.add(eh)
                        master_glyphs.append(ge)
                        added_in_sec += 1
                if added_in_sec > 0:
                    sec_end = len(master_glyphs) - 1
                    family_sections.append(
                        {
                            "family_hash": fm["family_hash"],
                            "start_idx": sec_start,
                            "end_idx": sec_end,
                            "new_glyphs": added_in_sec,
                            "member_blocks": len(fm["member_hashes"]),
                            "sample": fm["sample_strings"][0] if fm["sample_strings"] else "",
                        }
                    )

            if master_glyphs:
                m_png, _ = write_master_font_atlas_files(
                    fonts_dir=fonts_dir,
                    master_copy_dir=master_copy_dir,
                    lang=lang,
                    glyph_dim=dim,
                    glyphs=master_glyphs,
                    family_sections=family_sections,
                    total_member_blocks=total_blocks_dim,
                )
                total_master_font_atlases += 1
                m_line = (
                    f"[{lang:<6s}] MASTER_{lang}_font_{dim}x{dim:<4d} | MASTER {len(master_glyphs):4d}gl {dim}x{dim} "
                    f"({len(family_sections):2d} typefaces, {total_blocks_dim:3d} blocks) | 4bpp | -> {lang}/fonts/{m_png}"
                )
                lang_catalog_lines.append(m_line)
                master_catalog_lines.append(m_line)

        # Build dedicated Master Atlas for 16x16 GIM textures
        if combined_gim16:
            sorted_gim16 = [
                (h_hex, item[0], item[1], item[2])
                for h_hex, item in sorted(combined_gim16.items(), key=lambda kv: kv[0])
            ]
            res16 = write_master_16x16_gim_atlas(fonts_dir, master_copy_dir, lang, sorted_gim16)
            if res16 is not None:
                g16_png, _ = res16
                total_master_gim16_atlases += 1
                g16_line = (
                    f"[{lang:<6s}] MASTER_{lang}_gim_16x16   | MGIM16 {len(sorted_gim16):4d} tiles 16x16 "
                    f"(all 16x16 GIM tiles)        | 4/8b | -> {lang}/fonts/{g16_png}"
                )
                lang_catalog_lines.append(g16_line)
                master_catalog_lines.append(g16_line)

        # Optional: export individual merged families FAMILY_* (when --dump-families is enabled)
        if state.dump_families:
            for fam_hash, fam in sorted(families.items(), key=lambda kv: (-len(kv[1]["glyphs"]), kv[0])):
                if len(fam["member_hashes"]) > 1 or len(fam["glyphs"]) >= 8:
                    png_name, _ = write_family_font_files(fonts_dir, fam, lang)
                    total_dumped_families += 1
                    sample_preview = fam["sample_strings"][0] if fam["sample_strings"] else ""
                    entry_line = (
                        f"[{lang:<6s}] FAMILY_{fam_hash:016X} | FAMILY {len(fam['glyphs']):4d}gl {fam['glyph_dim']}x{fam['glyph_dim']} "
                        f"({len(fam['member_hashes']):3d} blocks) | 4bpp | -> {lang}/fonts/{png_name} (Sample: \"{sample_preview}\")"
                    )
                    lang_catalog_lines.append(entry_line)
                    master_catalog_lines.append(entry_line)

        # Optional: export individual font block atlases (when --dump-individuals is enabled)
        if state.dump_individuals:
            for finfo in sorted_fonts:
                h_norm = finfo["hash_norm"]
                fam_hash = font_to_family[h_norm]
                added = font_added_new_glyphs[h_norm]
                sources = combined_sources.get(h_norm, [])
                preview = finfo["strings"][0][:55] if finfo["strings"] else ""
                first_src = sources[0] if sources else ""

                is_redundant_subblock = (
                    not state.include_subtitles
                    and finfo["num_glyphs"] < 48
                    and added == 0
                    and h_norm != fam_hash
                )
                if is_redundant_subblock:
                    continue

                all_langs = state.hash_to_all_langs.get(h_norm, {lang})
                png_name, _ = write_individual_font_files(fonts_dir, finfo, fam_hash, lang, all_langs, sources)
                total_dumped_individuals += 1
                f_line = (
                    f"[{lang:<6s}] {h_norm:016X}        | FONT   {finfo['num_glyphs']:4d}gl {finfo['glyph_dim']}x{finfo['glyph_dim']} "
                    f"(Fam={fam_hash:016X}) | 4bpp | {first_src} -> {lang}/fonts/{png_name} (Text: \"{preview}\")"
                )
                lang_catalog_lines.append(f_line)
                master_catalog_lines.append(f_line)

        # Save bitmap GIM fonts (excluding 16x16 GIMs which are already in MASTER_<LANG>_gim_16x16.png)
        for h_hex, (img, meta, sources) in sorted(bucket.font_gims.items()):
            if meta["width"] == 16 and meta["height"] == 16 and not state.dump_individuals:
                continue
            os.makedirs(fonts_gim_dir, exist_ok=True)
            out_png_name = f"{h_hex}_{meta['width']}x{meta['height']}_psm{meta['psm']:02X}.png"
            img.save(os.path.join(fonts_gim_dir, out_png_name), "PNG")
            total_dumped_font_gims += 1
            first_src = sources[0] if sources else ""
            g_line = (
                f"[{lang:<6s}] {h_hex}        | FGIM   {meta['width']:4d}x{meta['height']:<4d} "
                f"(PSM=0x{meta['psm']:02X})        | {meta['bpp']}bpp | {first_src} -> {lang}/fonts_gim/{out_png_name}"
            )
            lang_catalog_lines.append(g_line)
            master_catalog_lines.append(g_line)

        # Copy native PNG fonts from extratex
        for src_full, rel_path in bucket.native_font_pngs:
            os.makedirs(fonts_gim_dir, exist_ok=True)
            fn = os.path.basename(src_full)
            parent_tag = os.path.basename(os.path.dirname(src_full))
            out_fn = f"{parent_tag}_{fn}" if parent_tag and parent_tag not in fn else fn
            shutil.copy2(src_full, os.path.join(fonts_gim_dir, out_fn))
            n_line = (
                f"[{lang:<6s}] NATIVE_FONT_PNG         | PNG                                      | 32b  | "
                f"{rel_path} -> {lang}/fonts_gim/{out_fn}"
            )
            lang_catalog_lines.append(n_line)
            master_catalog_lines.append(n_line)

        # If --all-textures was requested, also export all other GIM textures
        if state.all_textures:
            for h_hex, (img, meta, sources) in sorted(bucket.other_gims.items()):
                os.makedirs(all_gim_dir, exist_ok=True)
                out_png_name = f"{h_hex}_{meta['width']}x{meta['height']}_psm{meta['psm']:02X}.png"
                img.save(os.path.join(all_gim_dir, out_png_name), "PNG")
                total_dumped_other_gims += 1
                first_src = sources[0] if sources else ""
                o_line = (
                    f"[{lang:<6s}] {h_hex}        | GIM    {meta['width']:4d}x{meta['height']:<4d} "
                    f"(PSM=0x{meta['psm']:02X})        | {meta['bpp']}bpp | {first_src} -> {lang}/textures_gim/{out_png_name}"
                )
                lang_catalog_lines.append(o_line)
                master_catalog_lines.append(o_line)

        lang_cat_path = os.path.join(lang_root, f"font_hash_catalog_{lang}.txt")
        with open(lang_cat_path, "w", encoding="utf-8", newline="\n") as lcf:
            lcf.write("====================================================================================================\n")
            lcf.write(f" ACE COMBAT ZERO MASTER ATLAS & FONT CATALOG — LANGUAGE: {lang} ({SCANNER_VERSION})\n")
            lcf.write("====================================================================================================\n")
            for line in lang_catalog_lines:
                lcf.write(line + "\n")

    catalog_path = os.path.join(scan_out_root, "font_hash_catalog.txt")
    with open(catalog_path, "w", encoding="utf-8", newline="\n") as cf:
        cf.write("====================================================================================================\n")
        cf.write(f" ACE COMBAT ZERO UNIFIED MASTER GLYPH ATLAS CATALOG BY LANGUAGE & DIMENSION ({SCANNER_VERSION})\n")
        cf.write("====================================================================================================\n")
        cf.write("LANG     MASTER ATLAS / HASH     | TYPE & DIMENSION                         | BPP  | OUTPUT FILE\n")
        cf.write("----------------------------------------------------------------------------------------------------\n")
        for line in master_catalog_lines:
            cf.write(line + "\n")

    final_langs = [l for l in sorted_langs if not state.allowed_langs or l in state.allowed_langs]
    print("\n=====================================================================")
    print(f" MASTER GLYPH ATLAS GENERATION COMPLETE! ({SCANNER_VERSION})")
    print("=====================================================================")
    print(f" - Language folders in '{scan_out_root}':       {', '.join(final_langs) if final_langs else 'none'}")
    print(f" - All Master Atlases folder:                   {master_copy_dir}")
    print(f" - Unique dynamic font blocks found:            {total_unique_fonts_global}")
    print(f" - Language Master Font Atlases built:          {total_master_font_atlases} (per language & size 32x32 / 16x16)")
    print(f" - Master 16x16 GIM Atlases built:              {total_master_gim16_atlases}")
    if state.dump_families:
        print(f" - Additional FAMILY_* atlases exported:        {total_dumped_families}")
    if state.dump_individuals:
        print(f" - Additional individual font blocks exported:  {total_dumped_individuals}")
    print(f" - Large bitmap GIM fonts exported:             {total_dumped_font_gims}")
    if state.all_textures:
        print(f" - Other GIM textures exported:                 {total_dumped_other_gims}")
    print(f" - Master catalog written to:                   {catalog_path}")
    print("=====================================================================")


def run_scanner(
    extracted_root: str,
    scan_out_root: str,
    include_subtitles: bool = False,
    all_textures: bool = False,
    dump_families: bool = False,
    dump_individuals: bool = False,
    lang_filter: Optional[str] = None,
) -> None:
    if not os.path.exists(extracted_root):
        print(f"[ERROR] Folder '{extracted_root}' not found!")
        print("Specify the path to the unpacked game archives (e.g.: python ACZ_Resource_Scanner.py extracted)")
        return

    allowed_langs: Optional[Set[str]] = None
    if lang_filter:
        allowed_langs = {normalize_lang_code(x) for x in lang_filter.split(",") if x.strip()}

    os.makedirs(scan_out_root, exist_ok=True)
    state = FontScannerState(
        include_subtitles=include_subtitles,
        all_textures=all_textures,
        dump_families=dump_families,
        dump_individuals=dump_individuals,
        allowed_langs=allowed_langs,
    )

    mode_str = "UNIFIED MASTER ATLASES BY LANGUAGE (EN_US+EN_UK -> EN) & SIZE (32x32 / 16x16)"
    print(f"Scanning '{extracted_root}' -> '{scan_out_root}' [{mode_str}] ...")

    for root, _, files in os.walk(extracted_root):
        for fn in sorted(files):
            full_path = os.path.join(root, fn)
            rel_path = os.path.relpath(full_path, extracted_root)
            _, ext = os.path.splitext(fn)
            ext_low = ext.lower()
            path_lang = detect_lang_from_path(rel_path)

            if ext_low == ".png":
                if is_font_related_path(rel_path) or all_textures:
                    lang = normalize_lang_code(path_lang or "COMMON")
                    if state.is_lang_allowed(lang):
                        state.get_bucket(lang).native_font_pngs.append((full_path, rel_path))
                continue

            if ext_low in SKIP_NON_RESOURCE_EXTENSIONS:
                continue

            try:
                file_size = os.path.getsize(full_path)
                if file_size > 64 * 1024 * 1024 or file_size < 0x30:
                    continue

                with open(full_path, "rb") as f:
                    header32 = f.read(32)
                    if len(header32) < 32:
                        continue
                    # Fast skip for standalone large GIM textures when not font-related
                    if not all_textures and not is_font_related_path(rel_path) and header32[:4] == GIM_MAGIC:
                        w, h = struct.unpack_from("<HH", header32, 0x1C)
                        if not (w == 16 and h == 16):
                            continue

                    f.seek(0)
                    data = f.read()

                collect_from_node(data, rel_path, state, path_lang=path_lang)
            except Exception:
                pass

    build_master_atlases_and_dump(state, scan_out_root)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            f"Ace Combat Zero Master Glyph Atlas Scanner ({SCANNER_VERSION}: "
            "full scan across .MSA/.BFN/.BIN/.FHM/.DAT, EN_US+EN_UK -> EN, dedicated 32x32 & 16x16 atlases)."
        )
    )
    parser.add_argument(
        "extracted_dir",
        nargs="?",
        default=DEFAULT_EXTRACTED_DIR,
        help=f"Path to unpacked game archive directory (default: {DEFAULT_EXTRACTED_DIR})",
    )
    parser.add_argument(
        "out_dir",
        nargs="?",
        default=DEFAULT_SCAN_OUT_DIR,
        help=f"Output directory for per-language Master Atlases (default: {DEFAULT_SCAN_OUT_DIR})",
    )
    parser.add_argument(
        "--lang",
        default=None,
        help="Comma-separated language filter (e.g.: --lang EN or --lang EN,DE,JP)",
    )
    parser.add_argument(
        "--dump-families",
        action="store_true",
        help="Also save individual merged typeface files FAMILY_<HASH>.png",
    )
    parser.add_argument(
        "--dump-individuals",
        action="store_true",
        help="Also save individual font block files <HASH>_font_*.png",
    )
    parser.add_argument(
        "--include-subtitles",
        action="store_true",
        help="When using --dump-individuals, do not skip small 1-line subtitle blocks",
    )
    parser.add_argument(
        "--all-textures",
        "--all",
        action="store_true",
        dest="all_textures",
        help="Export all GIM textures in the game (not only fonts and 16x16 icons)",
    )
    args = parser.parse_args()
    run_scanner(
        extracted_root=args.extracted_dir,
        scan_out_root=args.out_dir,
        include_subtitles=args.include_subtitles,
        all_textures=args.all_textures,
        dump_families=args.dump_families,
        dump_individuals=args.dump_individuals,
        lang_filter=args.lang,
    )


if __name__ == "__main__":
    main()