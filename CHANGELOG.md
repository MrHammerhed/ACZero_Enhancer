# ACZero Enhancer (v0.21) — Project Changelog and File Registry

This document provides a registry of all modules, plugins, and tools for the **ACZero Enhancer** project for the standalone PC port of *Ace Combat Zero: The Belkan War*, along with the version history for each file.

## 1. Architecture and Component Overview

All DLL plugins located in the `mods\` directory are automatically loaded by the **`version.dll` (v1.2)** modloader via the `AczAddonInit` export. All plugins operate **completely independently of each other** (any plugin can be used standalone or in any combination).

| File | Current Version | Build Target (VS 2026 / 2022) | Configuration / Directory | Purpose |
| :--- | :---: | :--- | :--- | :--- |
| **`acz_hdtex.cpp`** | **v0.21** | C++17 DLL (`mods\acz_hdtex.dll`) | `mods\enhancer\acz_hdtex.ini`, `mods\textures\` | HD texture and dynamic font replacement/dumping engine (`GIM`, `FHM`, `TIM2`/`SWAD`), font metrics, and unified language Master Font Atlases (`MASTER_*`). |
| **`memdump.cpp`** | **v0.16** | C++17 DLL (`mods\memdump.dll`) | `mods\enhancer\memdump.ini`, `mods\memory_dumps\` | Independent diagnostic memory & state dumper (`F2` in-game hotkey, crash and exit hooks, pre-`Present` D3D12 `screenshot.png` capture, `acz_process.dmp` minidumps, auto-cleanup of old dumps). |
| **`ACZ_Textures_installer.bat`** | **v0.21** | Portable `.bat` + Python | `mods\textures\ostrichmods\`, `mods\textures\` | Standalone installer & converter for OstrichMods HUD pack (`actrilogy-hud-upgrade-v1.2.1.zip`) and `custom textures.zip` with Zero-Install portable Python environment. |
| **`py\ACZ_Resource_Scanner.py`** | **v9** | Python Tool | `py\` | Global scanner for extracted game archives (`a.arc`, `d.arc`) and builder for unified language master font atlases (`MASTER_<LANG>_font_*.png`). |
| **`py\ACZ_TIM2_Scanner.py`** | **v1** | Python Tool | `py\` | Universal `TIM2` scanner and converter for `.swa` (`SWAD`), `.fhm`, `.msa`, `.bin`, and `.tm2` containers to `PNG` + `.json` with triple-hash support. |
| **`py\ACZ_FHM_Converter.py`** | **v5** | Python Tool | `py\` | Container unpacker/packer for `.FHM`, font metric tables, and dynamic `*Pmenu.fhm` fonts with batch export mode for `acz_hdtex.dll`. |
| **`py\ACZ_GIM_PNG_Converter.py`** | **v5** | Python Tool | `py\` | Direct two-way `GIM <-> PNG` converter (4-bit `PSMT4` and 8-bit `PSMT8`, `CSM1` palette descrambling, 32-bit RGBA, `FNV-1a` 64-bit hashing). |

---

## 2. Component Changelogs

### 2.1. `acz_hdtex.cpp` (`mods\acz_hdtex.dll`) — Current Version: **v0.21**
Independent HD texture and font injection/dumping plugin for `version.dll` (v1.2). Configuration is stored in `mods\enhancer\acz_hdtex.ini`, replacement textures are loaded from `mods\textures\`.

* **v0.1 – v0.6 (Base GIM Interception & D3D12 Replacement):**
  * Hooked GIM initialization and descriptor binding functions in `acz.exe` (`+0x2038C0`, `+0x203B30`, `+0x191040`), implemented 64-bit `FNV-1a` hashing, and replaced D3D12 textures (`+0x214420`) with 32-bit RGBA PNGs from `mods\textures\`.
* **v0.7 – v0.10 (FHM Containers & Dynamic GameFont Fonts):**
  * Added parser hooks for `.FHM` sub-textures (`+0x203D90`, `+0x14CA50`, `+0x191A00`) and dynamic `GameFont` fonts (`+0x1FFBD0`, `+0x1FFCD0`) for `32x32`, `16x16`, and `64x64` glyphs with automated in-memory atlas generation.
* **v0.11 – v0.12 (Font Families `FAMILY_*` & Master Atlases `MASTER_*`):**
  * Implemented transitive font clustering into typefaces (`FAMILY_<HASH>.png`) and support for unified language master atlases `MASTER_<LANG>_font_32x32.png`, `MASTER_<LANG>_font_16x16.png`, and `MASTER_<LANG>_gim_16x16.png`.
* **v0.13 (Lock-Free Render Thread & Diagnostic Bridge Export):**
  * Implemented a 512-Kbit Bloom filter (`g_replaced_pixel_bloom`) and a 64K lock-free hash table (`g_fast_pixel_table`) eliminating render thread contention, plus exported bridge `AczHdTex_DumpLiveState` for memory dumps.
* **v0.14 (Multi-Hash `.json` & Direct GIM Interception):**
  * Added parsing for companion `<texture>.json` metadata files containing `"hashes"` arrays and direct `GIM\0` interception via `GsUploadVramBlock`.
* **v0.15 (Dimension Bounds `expected_w` / `expected_h` & Win32 Directory Scanner):**
  * Added validation of original texture dimensions (`expected_w`, `expected_h`) preventing hash collisions on reused `pixel_ptr` buffers, plus recursive directory traversal using Win32 `FindFirstFileW`.
* **v0.16 (Full TIM2 & `.swa` / SWAD Menu Archive Support):**
  * Implemented detection and replacement of `TIM2` (`.tm2`) textures, including textures inside `.swa` menu archives (`SWAD`: `AircraftSelect.swa`, `MainSelect.swa`, `Shop.swa`) with header recovery via `eXt\0` + `GIDX` signatures, scratch buffer interception at `TBP0 = 0x3000`, and automatic power-of-two padding (`40x40 -> 320x320` inside `512x512`).
* **v0.17 (Archive Texture Loader Hook & `000.png` Priority):**
  * Added hook on `LoadTextureFromArcPath` (`RVA +0x214230`), intercepting built-in `extratex/hudfont/000.png` and `001.png` textures prior to `.arc` archive lookup.
* **v0.18 (Menu / Minimap Artifact Fixes):**
  * Preserved native `0x00000001` / `0x00010001` PS2 GS VRAM flags for dithering backgrounds (`+0x1924E0`), and normalized transparent pixels (`A=0`) on `000.png` / `001.png` to `RGB=(105, 105, 105)`.
* **v0.19 (Smart Filtering for Master Font Atlases):**
  * Added `IsTileEmptyOrNearestPlaceholder`, automatically skipping empty cells and untouched 1x placeholders during master atlas slicing, saving ~250 MB RAM.
* **v0.20 (CLUT Deduplication via `AltHashes=`, Combined 16x16 + 32x32 Masters, Cumulative Font Dumper):**
  * Added support for `AltHashes=` in `.txt` files and `"glyphs_16x16"` in `MASTER_EN_font_32x32.json`, plus pre-loading existing dumped atlases on startup (`PreloadExistingDumpedMasterFontAtlases`).
* **v0.21 (Adv_X Core Centering, Baseline `std_h`, and Subtitle Bracket Assembly):**
  * In `BuildPerGlyphHdTexturesForFont`, glyph width windows (`adv_x * scale`) are centered strictly on the bright glyph core (`alpha >= 96`), eliminating cutoffs on wide-glow characters.
  * Added standard family baseline height calculation (`std_h`) for punctuation alignment and automatic assembly of the 4x HD subtitle brackets texture `0xBB10DED7AB6393C6ULL` from cells `#701..#703` of `MASTER_EN_font_32x32.png`.

---

### 2.2. `memdump.cpp` (`mods\memdump.dll`) — Current Version: **v0.16**
Independent diagnostic crash logger, memory dumper, and D3D12 framebuffer capture plugin. Configuration is stored in `mods\enhancer\memdump.ini`, dumps are saved to `mods\memory_dumps\`.

* **v0.1 – v0.12 (Base State Dumper & Exception Filter):**
  * Generates `memory_report.txt` (CPU registers, call stack, module RVAs, RAM stats) and intercepts unhandled exceptions via `SetUnhandledExceptionFilter`.
* **v0.13 (`F2` Hotkey, Exit IAT Hooks, Windows Minidump):**
  * Added `HotkeyMonitorThread` watching for **`F2`** keypresses in the active game window.
  * Added IAT hooks on `ExitProcess` (`RVA +0x27B2E8`) and `PostQuitMessage` (`RVA +0x27B6F0`) for clean exit dumps before the Windows loader lock is acquired.
  * Added Windows Minidump generation (`acz_process.dmp`) via `dbghelp.dll` (`MiniDumpWriteDump`), plus dynamic query to `acz_hdtex.dll` to dump 4MB PS2 GS VRAM and active textures.
* **v0.14 (Automatic Pre-Present `screenshot.png` Capture):**
  * Hooked `EndFrameAndPresent` (`RVA +0x20C280`) with asynchronous GPU DMA readback (`CopyTextureRegion`) to a double-buffered `D3D12_HEAP_TYPE_READBACK` heap right before swapchain presentation.
  * Automatically saves `screenshot.png` to the dump folder on `F2`, exit, or crash (using WIC or a standalone crash-safe C++ PNG encoder).
* **v0.15 (10-bit Swapchain Support & Startup Auto-Cleanup):**
  * Added support for `DXGI_FORMAT_R10G10B10A2_UNORM` (`fmt=24`) unpack in `SafeCopyReadbackToRgba`.
  * Added `CleanupOldMemoryDumpsOnStartup()`, automatically removing dumps older than `MaxDumpAgeHours=24`.
* **v0.16 (Decoupled Independent DLL):**
  * Extracted memory dumper into a standalone `mods\memdump.dll` with independent configuration (`mods\enhancer\memdump.ini`).
  * Dynamic interrogation of sibling modules (`acz_hdtex.dll`) via `GetModuleHandleA` / `GetProcAddress`.

---

### 2.3. `ACZ_Textures_installer.bat` — Current Version: **v0.21**
Standalone portable installer and converter for OstrichMods HUD pack (`actrilogy-hud-upgrade-v1.2.1.zip`) and `custom textures.zip`.

* **v1 – v4:** Cropping aircraft icons (`384x384 -> 288x288`), weapon icons (`256x256`), squadron emblems (16:9 to 4:3 correction), generation of multi-hash `.json` files, assembly of 36 weapon atlases (`2048x1024`), PS2-to-PC alpha correction (`* 255 / 128`), and generation of 60 hangar weapon `TIM2` textures (`320x320`).
* **v5 – v6:** Separation of hangar assets into categorized subdirectories; fixed combat HUD atlas source `ux\0.png` (`d00645d18395b834`) with zero-alpha normalization to `RGB=(105, 105, 105)`.
* **v7 – v0.20:** Assembly of 8x HD `MASTER_EN_font_64x64.png` (`8192x1024`, 23 letterhead glyphs) and 4x HD `MASTER_EN_font_32x32.png` (`2048x5632`, `128x128` tiles, 75% memory reduction) with unified `16x16` glyph definitions in `MASTER_EN_font_32x32.json`.
* **v0.21:** Full support for `custom textures.zip` (processing `For OstrichMods\` and deploying `textures\` to `mods\textures\`), integration of 38 squadron intro glyphs (`#601..#638`), generation of `subtitle_brackets_BB10DED7AB6393C6.png`, optimized PNG compression (`compress_level=4`), and **Zero-Install Portable Python Mode** (if Python/Pillow/NumPy is absent, automatically bootstraps an isolated runtime in `%TEMP%\ACZero_PyPortable`).

---

### 2.4. Python Utility Suite (`py\`)

* **`py\ACZ_Resource_Scanner.py` (v9):** Scans extracted `.arc` / `.fhm` containers, clusters font families (`FAMILY_*`), merges language slots (`1us` / `2uk` into `EN`), and builds unified master atlases (`MASTER_*`) with standard baseline alignment (`std_h`).
* **`py\ACZ_TIM2_Scanner.py` (v1):** Universal scanner for `TIM2` assets in `.swa` (`SWAD`), `.fhm`, `.msa`, `.bin`, `.dat`, `.tm2`; decodes 4-bit, 8-bit (`CSM1`), 16/24/32-bit textures with PS2 alpha scaling and triple-hash calculation for `acz_hdtex.dll`.
* **`py\ACZ_FHM_Converter.py` (v5):** Unpacker/packer for nested `.FHM` archives and font metric tables (`*Pmenu.fhm`) across all 7 language slots, with batch export for `acz_hdtex.dll`.
* **`py\ACZ_GIM_PNG_Converter.py` (v5):** Two-way converter for 4-bit (`PSMT4`) and 8-bit (`PSMT8`) `.gim` textures to 32-bit RGBA PNGs with `CSM1` descrambling and 64-bit `FNV-1a` naming.
