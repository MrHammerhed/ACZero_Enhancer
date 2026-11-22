# HD Terrain & Map Retexturing Architecture in Ace Combat Zero (PC Port)

This document summarizes the reverse-engineering findings, D3D12 `Texture2DArray` replacement pipeline, and asset workflow for high-definition terrain/map textures in **`acz_hdtex.asi`** (`v0.24`) and **`py/ACZ_Terrain_Converter.py`** (`v1.0`).

> **Development Stage Note (~25% Overall Terrain Progress — Stage 1)**:
> The `256x256x1024` 4x HD tile replacement implemented in `v0.24` represents **Stage 1** of the terrain overhaul. In the long-term rendering pipeline, these 4x HD tiles will serve as the **distant / high-altitude terrain textures** (preserving macro landmarks and painted distant foliage), while **close-range terrain** will be replaced with **PBR materials** (`Albedo + Normal + Roughness + Height`) rendered through a custom D3D12 terrain shader. See [`docs/MAP_EDITOR_ROADMAP.md`](MAP_EDITOR_ROADMAP.md) for the full multi-stage PBR terrain plan.

---

## 1. Native PC Terrain Texture Pipeline

Each gameplay map (`extracted\a\data\map\<map>\`) stores its ground texture tiles in four binary tables:
- **`<map>.mta`** — `16x16` coarse texture sector grid.
- **`<map>.mti`** — `256x256` tile index map referencing up to `1024` unique tiles.
- **`<map>.mtd`** — `1024` palettized `64x64` (`4096` bytes each) `PSMT8` tile pixel indices.
- **`<map>.mtc`** — 256-entry `CSM1`-swizzled RGBA CLUT palette (with PS2 half-range alpha `0x00..0x80`).

When a mission loads, `acz.exe` calls **`UploadTerrainTextureArray`** (`RVA +0x18E940` in `v1.0.2.1` / `RVA +0x18EFC0` in `v2.0.0.1`), which unpacks the `1024` tiles into a native **`64x64x1024`** D3D12 `Texture2DArray` and stores the resulting texture handle in `g_TerrainTexArray[slot_idx]` (`RVA +0x33BC50` in `v1.0.2.1` / `RVA +0x33EC70` in `v2.0.0.1`, 16 slots).

---

## 2. Runtime HD Replacement Hook (`Hook_UploadTerrainTextureArray`)

`acz_hdtex.asi` installs a 64-bit detour on `UploadTerrainTextureArray` (`Hook 11`):

1. **Tile Identification (64-bit `FNV-1a` Hashing)**:
   - `SafePrepareTerrainUploadInputs()` probes the incoming `clut_rgba` and `tile_pixels` buffers under SEH, descrambles the `CSM1` palette (`dst_idx = (i & 0xE7) | ((i >> 1) & 0x08) | ((i & 0x08) << 1)`) with PS2-to-PC alpha scaling (`min(a * 2, 255)`), and hashes each `4096`-byte `64x64` tile index array with 64-bit `FNV-1a`.
   - Each hash is matched against the lock-free content hash table (`LockFreeLookupContentHash`) populated during startup from `mods\textures\terrain\`.
   - If zero tiles match for the current slot, any previous HD array for that slot is released and execution falls back to the original `64x64x1024` uploader.

2. **Target Resolution & Upload Ring Buffer Guard**:
   - Default target array dimensions are **`256x256x1024` (`DXGI_FORMAT_BC7_UNORM`)**, requiring `64 MB` (`67,108,864` bytes) in the D3D12 upload ring slice.
   - Before allocating, `Hook_UploadTerrainTextureArray` inspects the current ring buffer usage (`GlobalUploadRingOffset & 0x07FFFFFF`). If `slice_used + 64 MB > 0x07FC0000`, it gracefully degrades to **`128x128x1024` (`16 MB`)**, and if the ring slice is still nearly full (or `AllocatePooledTexture` fails), it falls back cleanly to stock `64x64`.
   - Existing HD allocations in `g_HdTerrainTex[slot_idx]` are reused across reloads when `target_dim` matches, avoiding redundant pooled resource churn.

3. **Multi-Threaded Slice Assembly (`1..8` Worker Threads)**:
   - Up to `min(hardware_concurrency, 8)` worker threads process all `1024` slices in parallel via an atomic work counter:
     - **Fast `.dds_cache` Path (`TryLoadFastDdsTerrainSlice`)**: For opaque tiles (`!stock_has_alpha`) when `EnableDdsCache=1` and `target_dim == 256`, reads pre-compiled `256x256` `BC7_UNORM` blocks directly from `.dds_cache` (`ACZ3` tagged DX10 DDS), copying `64 KB` per slice with zero runtime BC7 encoding overhead.
     - **WIC Decode + BC7 Mode 6 Path (`TryEncodeHdPngTerrainSlice`)**: Decodes `.png`, `.jpg`, or `.jpeg` tiles via Windows Imaging Component (WIC), bilinearly resizes if dimensions differ from `target_dim`, and encodes to `BC7 Mode 6`.
     - **Stock Water Alpha Mask Preservation**: In `DrawMesh_p.cso`, terrain pixels with `alpha < 0x80` trigger the water shading branch. Because `.jpg` tiles (and opaque `.png` tiles) carry no alpha channel, whenever `stock_has_alpha == true` and the replacement image is fully opaque, `TryEncodeHdPngTerrainSlice` automatically bilinearly upsamples the stock `64x64` palette alpha channel into the HD tile before BC7 encoding.
     - **Unmatched Stock Tile Fallback (`EncodeStockTerrainTileSlice`)**: Any tile without an HD replacement is bilinearly upscaled from the stock `64x64` palette pixels and fast-encoded (`EncodeBlockBc7Mode6Solid` for uniform tiles, `EncodeBlockBc7Mode6Fast` otherwise) so partial packs mix seamlessly into the same `Texture2DArray`.

4. **Thread-Safe GPU Upload**:
   - The assembled `Texture2DArray` is uploaded through `g_UploadTextureToGpu(hd_tex)` (serialized via `g_UploadTextureGpuLock` SRWLock), bound into `g_TerrainTexArray[slot_idx]`, and the previous stock/stale texture handle is freed via `g_DestroyPooledTexture`.

---

## 3. Directory Layout & Supported Image Formats

- **Directory**: `mods\textures\terrain\<map_id>\<HASH>.jpg` (or `.png` / `.jpeg`), where `<HASH>` is the 16-character uppercase hexadecimal 64-bit `FNV-1a` hash of the tile's `4096`-byte index buffer.
- **Scanner Detection**: `ScanTexturesDirectory()` recursively scans `mods\textures\` for `.png`, `.jpg`, and `.jpeg` files and flags any texture under `\terrain\` in `g_SlotIsTerrainTile[]`.
- **Auto-Installer Integration**: When distributed inside `mods\custom textures.zip` under `textures/terrain/<map_id>/`, the native C++ auto-installer (`acz_tex_installer.h`, Step A) automatically extracts all terrain tiles into `mods\textures\terrain\` on launch.

---

## 4. Terrain Upscaling Workflow (`py/ACZ_Terrain_Converter.py`)

Because `64x64` tiles upscaled in isolation develop visible edge seams at `256x256`, **`py/ACZ_Terrain_Converter.py`** exports seamless context-padded sheets:
1. **Export**: Packs `6x6` core tiles + a `1`-tile seamless neighbor border (`8x8` = `512x512` sheets, which upscale `4x` to `2048x2048`) along with a `manifest.json`.
2. **Upscale**: Upscales sheets `4x` to `2048x2048`.
3. **Slice**: Crops the `1`-tile border and slices each `256x256` core tile back into `mods\textures\terrain\<map_id>\<HASH>.jpg` (or `.png`).

### Supported Maps in v0.24 (`18` Maps, `16,868` Tiles)
`k00a`, `m01a`, `m02a`, `m03k` *(shared by `m03m`, `m03n`, `r01a`, `v05a`)*, `m04k` *(shared by `m04m`, `m04n`)*, `m05a`, `m06a`, `m07a`, `m08k`, `m09a`, `m10k`, `m11a`, `m12a`, `m13a`, `m14k`, `m15a`, `m16k`, `m17a`.

---

## 5. Known Limitations & Notes

- **First-Load Cache Warmup**: The first time a map loads with `EnableDdsCache=1`, opaque `.jpg`/`.png` tiles are compiled into `.dds_cache` (`BC7_UNORM`). Subsequent mission loads read pre-compiled DDS slices directly.
- **Water Shoreline Alpha Resolution**: Tiles containing water preserve the stock `64x64` binary alpha mask (`< 0x80`), which can appear stair-stepped on narrow rivers (such as `m06a`) at very low altitude. See [`docs/KNOWN_VANILLA_ISSUES.md`](KNOWN_VANILLA_ISSUES.md) for per-map notes and planned shader/mask improvements.
