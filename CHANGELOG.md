# ACZero Enhancer (v0.25) — Project Changelog and File Registry

This document provides a registry of all modules, plugins, and tools for the **ACZero Enhancer** project for the standalone PC port of *Ace Combat Zero: The Belkan War*, along with the version history for each file.

## 1. Architecture and Component Overview

All ASI plugins located in the `mods\enhancer\` directory are automatically loaded by the custom **`version.dll` (v0.1)** ASI loader (`version_loader.cpp`) via `AczAddonInit` / `InitializeASI`, writing unified logs to `mods\enhancer\enhancer.log`. All plugins operate **completely independently of each other** (any plugin can be used standalone or in any combination).

| File | Current Version | Build Target (VS 2026 / 2022) | Configuration / Directory | Purpose |
| ----- | ----- | ----- | ----- | ----- |
| **`version_loader.cpp`** | **v0.1** | C++17 DLL (`version.dll`) | `mods\enhancer\enhancer.log` | Native `version.dll` proxy & ASI loader: hooks `acz.exe` `.text` startup after SteamStub unpacking, loads `mods\enhancer\*.asi`, writes unified `enhancer.log`, and chains `version_orig.dll` if present. |
| **`acz_version_map.h`** | **v0.21.1** | C++17 Header | Included in all ASIs | Automatic runtime version detection and piecewise RVA / RIP-disp32 mapping between `acz.exe` **v1.0.2.1** and **v2.0.0.1** (documented in `docs/VERSION_MAP.md`). |
| **`enhancer.cpp`** | **v0.25** | C++17 ASI (`mods\enhancer\enhancer.asi`) | `mods\enhancer\enhancer.ini` | Core widescreen, HUD, input, FastStartup, DrawDistance, RadialFog & frame-pacing engine: 60 FPS frame-pacing unlocker, multi-stage `FastStartup=0..3`, unified extended `DrawDistance` (`2x` artifact-free horizon + 19x19 grid + OOB fractal terrain + universal stage handler), spherical `RadialFog` D3D12 shader override, D3D12 texture/upload pool expansion, full 16:9/21:9 custom menus (`.swa`/`.swg`), gameplay HUD/radar calibration, cutscene/radio subtitles, Hor+ 21:9 3D FOV, DirectInput / XInput / Keyboard input fixes, and 2-Player Co-Op / VS Mode support. |
| **`acz_draw_distance.h`** | **v0.25** | C++17 Header | Included in `enhancer.cpp` | Modular extended `DrawDistance` engine: 19x19 coarse terrain grid, O(1) mirrored fractal OOB terrain with CPU-cached staging (`Hook_DrawMeshTemp` / `Hook_FlushMeshTemp`), universal stage environment handler (curated presets for Campaign/Free Mission/Free Flight + dynamic fallback for VS Mode / 2P / cutscenes), skybox-safe `FarZ` (`262,144.0f`), 60 FPS terrain LOD, and 524,288 WRAP cloud shadow map. |
| **`acz_radial_fog_shaders.h`** | **v0.25** | C++17 Header | Included in `enhancer.cpp` | Embedded SM 6.0 DXIL spherical fog shader overrides (`DrawMesh_g`, `DrawACM_v`, `DrawTree_v`) and version-independent `D3D12CreateDevice` -> `CreateGraphicsPipelineState` hook (regenerated via `py\build_radial_fog_shaders.py`). |
| **`acz_hdtex.cpp`** | **v0.24** | C++17 ASI (`mods\enhancer\acz_hdtex.asi`) | `mods\enhancer\acz_hdtex.ini`, `mods\textures\` | HD texture, map terrain, and dynamic font replacement/dumping engine (`GIM`, `FHM`, `TIM2`/`SWAD`, `256x256x1024` BC7 terrain `Texture2DArray`), `.png`/`.jpg`/`.jpeg` support, built-in native C++ texture pack auto-installer (`acz_tex_installer.h`), native controller button glyph selector (`pads/`), multi-threaded `.dds_cache` engine (BC7 Mode 6 / BGRA8), font metrics, and unified language Master Font Atlases (`MASTER_*`). |
| **`acz_tex_installer.h`** | **v1.0** | C++17 Header | Included in `acz_hdtex.cpp` | Zero-dependency native C++ ZIP extractor, WIC PNG resizer, PS2 alpha scaler, weapon atlas builder, and master font compiler replacing `ACZ_Textures_installer.bat`. |
| **`memdump.cpp`** | **v0.16.5** | C++17 ASI (`mods\enhancer\memdump.asi`) | `mods\enhancer\memdump.ini`, `mods\memory_dumps\` | Independent diagnostic memory & state dumper (`F2` in-game hotkey, crash and exit hooks, on-demand pre-`Present` D3D12 `screenshot.png` capture, `acz_process.dmp` minidumps, auto-cleanup of old dumps, sibling module state queries). |
| **`py\ACZ_Resource_Scanner.py`** | **v9** | Python Tool | `py\` | Global scanner for extracted game archives (`a.arc`, `d.arc`) and builder for unified language master font atlases (`MASTER_<LANG>_font_*.png`). |
| **`py\ACZ_TIM2_Scanner.py`** | **v1** | Python Tool | `py\` | Universal `TIM2` scanner and converter for `.swa` (`SWAD`), `.fhm`, `.msa`, `.bin`, and `.tm2` containers to `PNG` + `.json` with triple-hash support. |
| **`py\ACZ_FHM_Converter.py`** | **v5** | Python Tool | `py\` | Container unpacker/packer for `.FHM`, font metric tables, and dynamic `*Pmenu.fhm` fonts with batch export mode for `acz_hdtex.dll`. |
| **`py\ACZ_GIM_PNG_Converter.py`** | **v5** | Python Tool | `py\` | Direct two-way `GIM <-> PNG` converter (4-bit `PSMT4` and 8-bit `PSMT8`, `CSM1` palette descrambling, 32-bit RGBA, `FNV-1a` 64-bit hashing). |
| **`py\ACZ_SWAD_Converter.py`** | **v1.0.1** | Python Tool | `py\` | Container unpacker/packer and automated 16:9 / 21:9 widescreen layout generator for `.swa` (`SWAD` / `SWG`) menu archives. |
| **`py\ACZ_SWG_Visual_Editor.py`** | **v1** | Python Tool | `py\` | Interactive WYSIWYG GUI editor for `.swa` / `.swg` menu layouts, timelines, and matrix transformations (`4:3`, `16:9`, `21:9`). |
| **`py\ACZ_Terrain_Converter.py`** | **v1.0** | Python Tool | `py\` | Seamless context-padded `512x512` terrain sheet exporter (`mXX.mtd`/`.mtc`/`.mti`/`.mta`) and `4x` HD tile slicer (`256x256` FNV-1a hashed tiles) for `acz_hdtex.asi`. |
| **`py\build_radial_fog_shaders.py`** | **v1.0** | Python Tool | `py\`, `py\shaders\` | Compiles HLSL SM 6.0 spherical radial fog overrides (`DrawMesh_g.hlsl`, `DrawACM_v.hlsl`, `DrawTree_v.hlsl`) via DXC into `acz_radial_fog_shaders.h`. |

## 2. Component Changelogs

### 2.1. `enhancer.cpp` (`mods\enhancer\enhancer.asi`) — Current Version: **v0.25**

Independent widescreen display adaptation (`16:9` & `21:9` menus, gameplay HUD, subtitles, and 3D FOV), extended `DrawDistance` & spherical `RadialFog` engine, controller input engine, multi-stage `FastStartup`, and 60 FPS frame-pacing plugin for `version.dll` (v0.1). Configuration is stored in `mods\enhancer\enhancer.ini`. Operates fully standalone or in synergy with `acz_hdtex.asi`.

* **v0.1 – v0.15 (Base Memory Patching & Modloader Integration):**

  * Established modloader integration via `AczAddonInit` and safe virtual memory patching and x64 detour architecture.

* **v0.18 (Native Stage Environment Presets):**

  * Implemented native x64 ports of ACZGCT stage environment presets (`04_acz_extended_modifications.lua` curated 864-byte stage environment blocks for campaign missions and terrain clipping fixes).

* **v0.19 – v0.20 (Draw Distance Scaling):**

  * Added draw distance scaling across all 17 distance floats in stage environment blocks, camera `FarZ` (`+0x10`), and `RenderTerrain` inner/outer fade constants.

* **v0.21 (Draw Distance Overhaul, Modular Architecture & D3D12 Stability Hardening):**

  * Separated diagnostic memory dumper into standalone `memdump.dll` and core engine into `enhancer.dll` (`mods\enhancer\enhancer.ini`).

  * Expanded coarse terrain tile grid (`19x19` at `2x`) via `RenderExpandedCoarseTerrainGrid`.

  * Expanded D3D12 upload ring buffers 4x (48MB/16MB), `DrawMeshTemp` instance reservation 4x (3.86MB), and `InitCPT` VB 4x (328KB), eliminating D3D12 device removal crashes under heavy rendering loads.

  * Overhauled dynamic cloud shadow map to full `524,288m x 524,288m` 1:1 WRAP coverage anchored at world origin `(0, 0)`, eliminating camera-step shadow jumps.

  * Fixed ground emitter array overflow during cutscenes and replays; scaled camera `FarZ` across all 20 camera sites to eliminate skybox corner clipping.

* **v0.21.1 (Dual-Version Executable Support):**

  * Integrated `AczRva` and automatic RIP-relative `disp32` adjustment across all hooks and memory patches for automatic compatibility with both `v1.0.2.1` and `v2.0.0.1`.

* **v0.22 (Procedural OOB Terrain Clamping):**

  * Added precomputed O(1) 2D-mirrored + procedural fractal out-of-bounds terrain extension with CPU-cached D3D12 hardware UV-mirroring (`Hook_DrawMeshTemp` & `Hook_FlushMeshTemp`), eliminating write-combined RAM stalls.

* **v0.22.1 (Initial Fast Startup Mode):**

  * Added initial `FastStartup` mode skipping the native launcher check, controller manual screen, intro logos, and title screen (later expanded into multi-stage `FastStartup=0..3` in `v0.24`).

* **v0.23.0 (Widescreen 16:9 & 21:9 Menu Engine):**

  * Full-screen custom 16:9 and 21:9 menu layout support across all 31 in-game menus (`<menu>_16x9.swa`, `<menu>_21x9.swa`) with dynamic aspect switching, 9-slice frame scaling, and edge-to-edge coverage (`Hook_CSwgInstanceInit`, `Hook_CSwgInstanceUpdate`, `Hook_CSwadMenuDraw`).

  * Implemented `ApplyEngine21x9AspectPatches` (`OptionSetAspect=2`), save file persistence, dynamic 4:3 pillarbox scissor, and GS virtual width management (682 / 896).

  * Added dynamic orthographic projection scaling for briefing videos (4:3 pillarbox on 21:9) and cutscene/interview videos (`Hook_DrawMovieQuad`).

* **v0.23.2 (60 FPS Frame-Pacing Unlocker & Title Card Support):**

  * Added 60 FPS frame-pacing unlocker at `RVA +0x20BCAF` (`BeginFrame +0x20B9C0`), preventing temporary frame drops from trapping the engine in a permanent 30.0 FPS catch-up loop.

  * Expanded D3D12 placed-resource texture pools (`RVA +0x2A9DE0`) to 64MB (8 slots), 32MB (32 slots), 6.25MB (128 slots), 2.7MB (128 slots), 1.1MB (256 slots), and 320KB (512 slots).

  * Added widescreen 16:9/21:9 support for mission title cards (`Hook_MissionTitleDraw` `+0x173120`) and loading screen epigraph quotes (`Hook_LoadingScreenDraw` `+0x205050`).

* **v0.23.3 (Gameplay HUD, Radio Subtitles, Cutscenes, Debriefing, 21:9 3D FOV & DirectInput Controller Compatibility):**

  * **PlayStation 5 DualSense & DualShock 4 DirectInput Compatibility (`Hook_PollDirectInputDevice` `+0x21A480`):** Fixed unrecognized Sony Wireless Controller (`VID 0x054C`) DirectInput mappings where `settings/dinput.ini` (`[00000000]`) previously scrambled face buttons (`Square`/`Cross`/`Circle`), mapped the right stick to `Rx`/`Ry` (`L2`/`R2` trigger axes), left `L3` (`B11`), `R3` (`B12`), and `Touchpad Click` (`B14`) unmapped, and initialized default `HatL..HatD` (`+0x33B1C0`) as buttons `33..36` (`type=0`) instead of `POV Hat` (`type=1`). All DualSense / DS4 buttons (`Cross`, `Circle`, `Square`, `Triangle`, `L1`, `R1`, analog `L2`/`R2`, `Create`/`Share`, `Options`, `L3`, `R3`, `Touchpad Click`, and `D-Pad` POV Hat) now work natively out-of-the-box via USB and Bluetooth.

  * **Hor+ 21:9 3D FOV (`Hook_GsBuildProjectionMatrices` `+0x1FBEC0` & `Hook_GetAspectScaleY` `+0x1FD130`):** Expands `GsScreenConfig` virtual width in 21:9 mode to 840.0f, producing true Hor+ 3D camera FOV, full-width frustum clipping planes, and unstretched 1:1 3D reticles, target boxes, and compass.

  * **Gameplay HUD Adaptation (`Hook_CHudManagerDraw` `+0x0AE510` & Widget Hooks):** Unstretches and shifts `SCORE`, `TIME`, and destroyed target list (`Widget7`) towards the top corners; shifts `SPEED` and `ALT` towards screen corners strictly in 3rd-person view (`*(sub_speed + 0x34) == 0`) while keeping HUD-bracket alignment in cockpit/1st-person views; relocates inactive wingman command submenu (`COVER`) compactly above the minimap; clamps 1-px weapon range bracket; compacts target marker labels (`TGT`/`FRND`, aircraft type, ace name, distance); bypasses hardcoded 16:9 radar scissor lookup table (`RVA +0x19451B`).

  * **Subtitles & Text Scaling (`Hook_RadioSubFontDraw` `+0x1A5100`, `Hook_CutsceneSubFontDraw` `+0x00B410`, `Hook_CutsceneSubFontDrawEmblem` `+0x00B810`, `Hook_CutsceneFontDraw` `+0x1FCBA0`):** Uniform 12.4 subpixel fixed-point X-scaling for in-flight radio subtitles, real-time 3D cutscene dialogues, location/time captions, FMV teletype text, and squadron intro titles & emblems.

  * **Post-Mission Debriefing (`Hook_DebriefingDraw` `+0x016E30`):** Enables full-viewport widescreen scissor override for `MISSION RESULT` and expands 3D tactical map frustum planes edge-to-edge.

  * **Cross-DLL Synergy & Standalone Graceful Fallback:** Integrates dynamic runtime bridges (`AczHdTex_GetTim2AspectMask`, `AczHdTex_ResolveGimDescriptorAspectTier`, `AczHdTex_GetActiveTextureAspectTier`) to synchronize widescreen `.swa` menus with `_16x9.png` / `_21x9.png` HD textures when `acz_hdtex.dll` is present, while maintaining full standalone functionality and in-memory `select.msa` 21:9 option patching (`Hook_FallbackGameFontInit`) when running alone.

* **v0.23.4 (Input Engine Overhaul, 2-Player Co-Op / VS Mode Support, Pause Menu Fixes & Engine Heap/DMA Expansion):**

  * **Radial Stick Deadzones & Softened DualSense/DS4 HID Vibration:** Added smooth radial deadzones (`16%` Left Stick, `18%` Right Stick) in native PlayStation DirectInput mode to eliminate stick drift, and softened raw HID output reports (`0x02` USB / `0x31` Bluetooth) to `60%` low-frequency rumble and capped high-frequency motor intensity (`80/255`) to match XInput vibration feel.

  * **DualSense Touchpad Mapped to View (Radar Switch):** Mapped DualSense Touchpad Click (`Button 14` / `rgbButtons[13]`) directly to `View / Select` (radar size toggle) instead of duplicating `Create / Share`.

  * **Configurable 2-Player Co-Op / VS Mode Support (`Unlock2PlayerCampaign = 0 | 1` in `enhancer.ini`):**

    * **`Unlock2PlayerCampaign=0` (Default — Single-Player Seamless Switching):** Merges Keyboard (`st1`) and Gamepad (`st0`) inputs into Player 1 (`ControllerState[0]`, `+0x33ABE0`) so both devices control Player 1 simultaneously, while marking Player 2 (`ControllerState[1]`, `+0x33AC10`) disconnected (`status = 1`, `pad_mode = 0`) so `2P NEW GAME` and `VS MODE` stay locked in single-player.

    * **`Unlock2PlayerCampaign=1` (Two-Player Split Input):** Keeps Player 2 active (`status = 0`, `pad_mode = 0x79`) and decouples Keyboard from Gamepad so **Player 1** is controlled exclusively by Gamepad and **Player 2** is controlled independently by Keyboard in 2-Player Co-Op and VS Mode.

  * **Expanded Main Engine Heap & Per-Frame 3D GIF DMA Stream Buffer (`RVA +0x1FF35C`..`+0x1FF456`):** Expanded the main engine heap (`64 MB -> 128 MB`) and the double-buffered per-frame 3D GIF DMA packet stream buffer (`2 MB -> 8 MB` total, `1 MB -> 4 MB` per frame) to prevent DMA buffer overflow into the adjacent GS display context `*(0x14032c8b8)` (`0xC0000005` at `RVA +0x1FFA2D`).

* **v0.23.5 (Save-Safe `ScreenAspect` Persistence & Widescreen Menu Scrollbar Thumb Alignment, Texture Pack Auto-Installer Release Alignment):**

  * **Save-Safe Aspect Ratio Persistence (`[Widescreen] ScreenAspect = 0 | 1 | 2` in `enhancer.ini`):** Hooks `SwgOptionSetAspect` (`RVA +0x1F3850`), `CSysData::Save` (`RVA +0x1895B0`), and `CSysData::Load` (`RVA +0x1895E0`) so the save file on disk always stores vanilla-compatible `16:9` (`1`) while the active runtime aspect ratio (`0 = 4:3`, `1 = 16:9`, `2 = 21:9`) is persisted in `enhancer.ini` and synchronized every frame in `SyncSwgInstanceAspect`.
  
  * Synchronized release versioning with `acz_hdtex` v0.23.5 (`acz_tex_installer.h` integration).

* **v0.23.6 (Custom `version.dll` ASI Loader & Shared `mods\enhancer\enhancer.log`):**

  * Migrated build target to `mods\enhancer\enhancer.asi` (exporting both `AczAddonInit` and `InitializeASI`) for the custom `version.dll` (v0.1) loader, unified logging into `mods\enhancer\enhancer.log`, and added sibling detection for `mods\enhancer\acz_hdtex.asi`.

* **v0.24 (Multi-Stage `FastStartup` Engine & Press-to-Skip Intro Logos):**

  * Ported and expanded `FastStartup` in `[Enhancer]` (`mods\enhancer\enhancer.ini`, default `0`) into four configurable stages (`0..3`) with full dual-version (`v1.0.2.1` & `v2.0.0.1`) RVA support:
    * **`FastStartup=0` (Default):** Disabled (vanilla startup sequence).
    * **`FastStartup=1`:** Bypasses the native launcher (`e.arc` mount & dialog) and controller manual screen (`CModePadManual` at `AczRva(0x16348B)`), and installs `Hook_CModeWakeLogoUpdate` (`AczRva(0x17DC30)`) so pressing any gamepad or keyboard button (`Enter` / `Space` / `Esc`) during intro logos cleanly transitions directly to the Title Screen (`CModeTitle`, `+0x1782B0`) with brief input suppression to prevent accidental auto-advance.
    * **`FastStartup=2`:** Level 1 + removes the 2×30-frame (~1.0s) artificial Memory Card UI timers in `CSelectMemorycard` (`modeWake` at `AczRva(0x1C3000)` and `modeLoad` at `AczRva(0x1C2B27)`) and automatically redirects post-save boot (`AczRva(0x17D513)`) straight to the Title Screen (`CModeTitle`).
    * **`FastStartup=3`:** Level 2 + automatically skips both intro logos and the Title Screen straight into the Main Menu (`CModeMainSelect`, `+0x178330`).

* **v0.24.1 (Steam `v2.0.0.1` Startup Diagnostics & `acz_version_map.h` Sync):**

  * Added detailed `[V2.0.0.1-DIAG]` startup diagnostics (PE header/sections, pre/post-patch `pool_tbl` dump, 60 FPS site bytes, and `ScreenSizeOption`) logged to `mods\enhancer\enhancer.log` whenever `acz.exe` `v2.0.0.1` is detected.
  * Synced `acz_version_map.h` with the `.rdata` `< 0x2CEEF0` `+0x2040` shift (`D3D12TexturePoolTable` `0x2A9DE0 -> 0x2ABE20`).

* **v0.25 (Unified `DrawDistance` Engine, Universal Stage Support & Spherical `RadialFog`):**

  * **Spherical Radial Fog (`[DrawDistance] RadialFog = 1 | 0`, default `1`):** Replaces vanilla planar Z-depth fog (`clip_w`) with true 3D spherical distance in `DrawMesh_g` (terrain), `DrawACM_v` (3D models), and `DrawTree_v` (trees) via a version-independent `D3D12CreateDevice` IAT hook -> `ID3D12Device::CreateGraphicsPipelineState` vtable hook (`acz_radial_fog_shaders.h`). Active in both vanilla (`DrawDistance=0`) and extended (`DrawDistance=1`) modes across 4:3, 16:9, and 21:9, preventing distant geometry from popping in at screen corners while rotating the camera.
  * **Unified Extended `DrawDistance` (`[DrawDistance] DrawDistance = 0 | 1`, default `0`):** Modular rework (`acz_draw_distance.h`) providing a single artifact-free `2x` extended draw distance mode with 19x19 coarse terrain grid (`RenderExpandedCoarseTerrainGrid` `+0x077F00`), automatic O(1) precomputed 2D-mirrored fractal OOB terrain extension (`Hook_DrawMeshTemp` `+0x198720` CPU-cached staging + single-stream `Hook_FlushMeshTemp` `+0x1993F0` GPU UV-mirroring), 60 FPS high-altitude Low-LOD transition (`terrain_far_inner = 65,536.0f`), skybox-safe `FarZ` (`262,144.0f`), and full `524,288 x 524,288` world-origin WRAP cloud shadow map.
  * **Universal Stage & Mode Support (`Hook_ParseStageContainer` `+0x0C80E0`):** Applies curated atmosphere-aware presets for all Campaign (`0..30`), Free Flight (`42..72`), Free Mission (`78..108`), Gauntlet, and Valais stage IDs, and dynamically extends native 864-byte stage environment blocks (`toc[19]`, `toc[38]`, `sub_toc[19]`, `sub_toc[38]`) for all remaining stages and modes (VS Mode `v01a`–`v08a`, `fr1a`, 2P Co-Op, takeoff/landing/refueling, cutscenes, Scene Viewer).
  * **Dual-Version `v2.0.0.1` RIP-`disp32` Sync & Cross-Plugin Compatibility:** Restored automatic RIP-relative `disp32` adjustment in `PatchMemoryBytes` for `acz.exe` `v2.0.0.1`, expanded `AczEnhancer_WriteStateSummary` for `memdump.asi` (`F2` with per-frame QPC telemetry), and added `AczEnhancer_GetDrawDistanceState` export for sibling ASI/DLL plugins.

### 2.2. `acz_hdtex.cpp` (`mods\enhancer\acz_hdtex.asi`) — Current Version: **v0.24**

Independent HD texture and font injection/dumping plugin for `version.dll` (v1.2). Configuration is stored in `mods\enhancer\acz_hdtex.ini`, replacement textures are loaded from `mods\textures\`. Operates fully standalone or in synergy with `enhancer.dll`.

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

* **v0.21.1 (Dual-Version Executable Support):**

  * Integrated `acz_version_map.h` (`DetectAczExeVersion`, `AczResolveRva`) and dynamic prologue verification (`+0x0680` / `+0x0040` fallback in `InstallDetour64`) across all texture hooks for both `v1.0.2.1` and `v2.0.0.1`.

* **v0.22.0 – v0.22.1 (Aspect-Ratio Variant Binding & In-Memory Sliced Caching):**

  * Added aspect-ratio variant detection (`_16x9.png`, `_21x9.png`) attaching multiple widescreen variants to a single slot, with real-time tier selection (`ResolveSlotEffectiveAspectTier`).

  * In-memory sliced PNG caching (`mem_png`) for dynamic fonts and 16x16 GIM tiles with zero disk re-reads.

* **v0.23.0 (Native Gamepad & Keyboard Glyph Selector):**

  * Added modular input glyph replacement engine (`mods\textures\pads\` and `pad_*.json` selectors) supporting PlayStation DualSense (`PS`), Xbox Series (`XBOX`), and Keyboard (`KB`) prompt sets across all menus and HUD prompts, with automatic active-device detection (`GamepadGlyphs=AUTO`).

* **v0.23.2 (Autonomous DDS Cache Engine & sRGB Normalization):**

  * Built autonomous multi-threaded `.dds_cache` engine compiling replacement PNGs to `BC7 Mode 6` (opaque/alpha) and `BGRA8` on the fly, loading textures directly into D3D12 with zero runtime PNG decode overhead.

  * Embedded ICC profile parser converting Display P3 and wide-gamut PNGs to standard linear sRGB before D3D12 upload, preventing color oversaturation.

  * Thread-safe GPU upload serialization (`Hook_UploadTextureToGpu` `+0x20DCF0`) with `SRWLock` protecting D3D12 ring buffers against worker-thread race conditions.

* **v0.23.3 (Universal Companion Metadata, Live Controller/Keyboard Auto-Detect & Architecture Cleanup):**

  * **Live In-Game Controller & Keyboard Auto-Switching (`GamepadGlyphs = AUTO | PS | XBOX | KB`):** Polls live per-frame input activity across `ControllerState[0]` (Gamepad, `+0x33ABE0`) and `ControllerState[1]` (Keyboard, `+0x33AC10`) as well as connected HID / XInput / DirectInput / Steam Input devices, dynamically swapping both the D3D12 resource pointer (`+0x00`) and the D3D12 SRV pool handle (`+0x28`) on the fly during gameplay without restarting the game.

  * Dynamic dimension tracking (`RecordSlotExpectedDims`, `match_alt`, `match_double`) allowing single HD textures to replace varied-dimension GIM/TIM2 targets.

  * Clean architectural decoupling: extracted widescreen display and HUD logic to `enhancer.dll`, established cross-DLL bridge exports (`AczHdTex_GetTim2AspectMask`, `AczHdTex_ResolveGimDescriptorAspectTier`, `AczHdTex_GetActiveTextureAspectTier`), and added standalone D3D12 texture pool expansion (`EnsureD3D12TexturePoolsExpanded`).

* **v0.23.4 (Controls Manual Diagram Fix, 2P Co-Op Config Sync & Build Fixes):**

  * **Synchronized `Unlock2PlayerCampaign` INI Parsing:** Reads `Unlock2PlayerCampaign` from both `acz_hdtex.ini` and `enhancer.ini` using valid 4-argument `GetPrivateProfileIntA` calls (`g_ini_path.c_str()` and sibling `enhancer.ini` path), resolving `C2660` / `C2065` build errors.

* **v0.23.5 (Built-In Native C++ Texture Pack Auto-Installer, Aircraft Icon Mapping Fix & 4:3 Tier Caching Fix):**

  * **Built-In Native C++ Texture Pack Auto-Installer (`acz_tex_installer.h`):** Ported the entire OstrichMods PCSX2 HUD & Hangar converter and custom texture pack installer directly into C++ inside `acz_hdtex.dll` (`AutoInstallTexturePacksIfPresent`, controlled by `AutoInstallTexturePacks=1` and `DeleteSourceArchivesAfterInstall=0` in `acz_hdtex.ini`). Includes a self-contained RFC 1951 `DEFLATE` / `ZIP` reader, WIC PNG decoder/encoder, separable Lanczos-3 premultiplied-alpha resampler, PS2-to-PC alpha scaler, 36 composite `2048x1024` hangar weapon atlas builder, 60 `320x320` TIM2 hangar weapon icons, 19 `512x512` TIM2 weapon trajectory diagrams, and `8x`/`4x` Master Font Atlas builder (`MASTER_EN_font_64x64.png`, `MASTER_EN_font_32x32.png` + `.json` with `16x16` donor mapping). Completely eliminates `mods\ACZ_Textures_installer.bat` for full NexusMods and antivirus compatibility.

  * **Fixed `zoef` (ADF-01 FALKEN) & `mrgn` (ADFX-01/02 Morgan) Aircraft Icon Mapping:** Corrected the swapped source hash mapping between `zoef_D6E7F13AB1C907BF.png` and `mrgn_4579101E74130E99.png` in `kAircraftMap` and bumped `.install_stamp` to `ACZ_NATIVE_INSTALLER_V2`.

  * **Fixed `4:3` Base Texture Tier Caching (`GetOrLoadCustomTexForTier`):** Fixed a bug where switching to `4:3` (`eff_tier == 0`) after loading `16:9` or `21:9` menu textures did not update `g_SlotLoadedTexTier[0][id]`, causing `4:3` menus to reuse cached widescreen scrollbar frames.

* **v0.23.6 (Custom `version.dll` ASI Loader & Shared `mods\enhancer\enhancer.log`):**

  * Migrated build target to `mods\enhancer\acz_hdtex.asi` (exporting both `AczAddonInit` and `InitializeASI`) for the custom `version.dll` (v0.1) loader and unified logging into `mods\enhancer\enhancer.log`.

* **v0.24 (HD Terrain `Texture2DArray` Retexturing Engine & `.jpg`/`.jpeg` Support):**

  * **Native Terrain `Texture2DArray` Hook (`Hook_UploadTerrainTextureArray`, `RVA +0x18E940` in `v1.0.2.1` / `+0x18EFC0` in `v2.0.0.1`):** Intercepts the engine's `64x64x1024` palettized terrain texture array upload (`g_TerrainTexArray[16]` at `+0x33BC50` / `+0x33EC70`), hashes all 1024 `4096`-byte tile index arrays via 64-bit `FNV-1a`, and upgrades matched maps to a **`256x256x1024` `BC7_UNORM`** D3D12 `Texture2DArray` (with automatic `128x128x1024` and stock `64x64` fallbacks if the D3D12 upload ring buffer slice is nearly full). This represents **Stage 1 (~25%)** of the terrain overhaul: these 4x HD tiles will serve as **distant terrain textures** in future updates, while close-range terrain will transition to **PBR materials** via a custom D3D12 terrain shader (see `docs/TERRAIN_RETEXTURE_ARCHITECTURE.md` and `docs/MAP_EDITOR_ROADMAP.md`).

  * **Multi-Threaded Slice Assembly, Fast `.dds_cache` Path & Water Alpha Preservation:** Uses up to 8 worker threads to assemble all 1024 slices in parallel — reading pre-compiled `256x256` `BC7_UNORM` blocks directly from `.dds_cache` (`TryLoadFastDdsTerrainSlice`) for opaque tiles, decoding/encoding `.png`/`.jpg`/`.jpeg` via WIC + `BC7 Mode 6` (`TryEncodeHdPngTerrainSlice`), bilinearly preserving the stock `64x64` water alpha mask (`alpha < 0x80`) whenever an opaque HD tile replaces a water tile, and bilinearly upscaling unmatched stock tiles (`EncodeStockTerrainTileSlice` with solid-block fast path).

  * **`.jpg` / `.jpeg` Texture Support:** Extended `ScanTexturesDirectory()` and WIC decoding to load `.jpg` and `.jpeg` replacement textures alongside `.png`, significantly reducing disk and archive footprint for large HD terrain packs (`mods\textures\terrain\`).

* **v0.24.1 (DDS Cache Startup Crash Guard, 2 GB Auto-Installer Archive Support & `v2.0.0.1` Diagnostics):**

  * **Raised Native Texture Pack Auto-Installer Archive Limit (`512 MB -> 2 GB` in `acz_tex_installer.h`):** Fixed `ReadWholeFileBytes` silently rejecting the new `v0.24` `custom textures.zip` (`728 MB`) on clean installs because of a hardcoded `512 MB` cap, and added explicit warning logging if an archive fails to open.
  * **SEH Crash Guard & Automatic PNG Fallback (`SafeAllocatePooledTextureSeh`):** Wrapped `g_AllocatePooledTexture` inside `LoadTextureFromDdsCacheFile` in a POD SEH guard. If a GPU/driver, Steam `v2.0.0.1`, or Linux Proton setup throws an exception (`0xC0000005`) during `.dds_cache` texture allocation, `acz_hdtex.asi` logs the exact texture, dimensions, format, executable version, and `UploadRingOffset`, disables `.dds_cache` for the current session, and falls back cleanly to `CreateTextureFromPngMemory` without crashing.
  * **Explicit `EnableDdsCache=1` in `mods\enhancer\acz_hdtex.ini`:** Exposed `EnableDdsCache` directly in the default configuration and updated legacy `modloader.log` comments to `mods\enhancer\enhancer.log`.
  * **Steam `v2.0.0.1` Diagnostics (`[V2.0.0.1-DIAG]`):** Logs resolved D3D12/GS/Terrain function addresses & prologue bytes, `pool_tbl` values, per-hook status mask (`ok1..ok11`) on failure, and the first 32 DDS allocations when `acz.exe` `v2.0.0.1` is detected.

### 2.3. `memdump.cpp` (`mods\enhancer\memdump.asi`) — Current Version: **v0.16.5**

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

* **v0.16.1 (Dual-Version Executable Support):**

  * Dynamically resolves `EndFrameAndPresent` (`+0x0680`), `.rdata` IAT slots (`ExitProcess` `+0x2010`, `PostQuitMessage` `+0x1FF8`), and `.data` D3D12 globals (`+0x3020`) across both `v1.0.2.1` and `v2.0.0.1`, recording the detected executable version in `memory_report.txt`.

* **v0.16.2 (Multi-Module Cross-DLL Diagnostics):**

  * Added runtime bridge queries to `enhancer.dll` (`AczEnhancer_WriteStateSummary`) and `acz_hdtex.dll` (`AczHdTex_DumpLiveState`), recording active widescreen/input state and VRAM replacement statistics into diagnostic reports.

  * Added `VectoredContinueHandler` fallback (`AddVectoredContinueHandler`) for fatal hardware exceptions, post-`WinMain` `SetUnhandledExceptionFilter` re-assertion on the first frame of `EndFrameAndPresent`, and an atomic single-dump guard.

* **v0.16.3 (On-Demand D3D12 Backbuffer Readback & Fence Synchronization):**

  * Optimized `Hook_EndFrameAndPresent` (`RVA +0x20C280`) D3D12 backbuffer readback to execute on-demand when `F2` is pressed (`g_RequestImmediateCapture` with GPU command queue fence wait `WaitForSlotFenceCompletionSeh`), plus a low-overhead 59-frame alternating background refresh for crash dumps instead of copying the full 4K backbuffer over PCIe every frame.

  * Added non-zero RGB validation (`non_zero_rgb != 0`) in `SafeCopyReadbackToRgba` to prevent saving blank black screenshots from unrendered backbuffer slots.

  * Synchronized dual-version RVA mapping thresholds (`0x002CEEF0` `.rdata` table alignment and `0x00324600` `BSS` globals threshold) with `acz_version_map.h`.

* **v0.16.4 (Release Alignment with v0.23.5):**

  * Synchronized module version alongside the v0.23.5 release.

* **v0.16.5 (Custom `version.dll` ASI Loader & Shared `mods\enhancer\enhancer.log`):**

  * Migrated build target to `mods\enhancer\memdump.asi` (exporting both `AczAddonInit` and `InitializeASI`) for the custom `version.dll` (v0.1) loader, updated sibling runtime bridges to query `enhancer.asi` and `acz_hdtex.asi`, and unified logging into `mods\enhancer\enhancer.log`.

* **v0.16.6 (Steam `v2.0.0.1` Diagnostic RVA Logging):**

  * Added `[V2.0.0.1-DIAG]` startup logging of resolved `EndFrameAndPresent`, IAT, and D3D12 global RVAs in `mods\enhancer\enhancer.log` when Steam `acz.exe` `v2.0.0.1` is detected.

### 2.4. `version_loader.cpp` (`version.dll`) — Current Version: **v0.1**

Custom native `version.dll` proxy and ASI plugin loader for `acz.exe`.

* **v0.1 (Initial Release):**

  * Implements all 17 standard `version.dll` proxy exports forwarding to `System32\version.dll`.

  * Hooks `kernel32.dll` startup IAT slots (`GetSystemTimeAsFileTime`, `GetModuleFileNameA`) with `.text` section return-address verification after SteamStub unpacking, loads all `*.asi` plugins from `mods\enhancer\` in priority order (`memdump.asi`, `enhancer.asi`, `acz_hdtex.asi`), writes unified logs to `mods\enhancer\enhancer.log`, and optionally chains a renamed legacy loader (`version_orig.dll`, `version_original.dll`, or `modloader.dll`).

### 2.5. `ACZ_Textures_installer.bat` — Legacy / Superseded by `acz_tex_installer.h` (v8.1)

Native C++ port of the OstrichMods HUD pack (`actrilogy-hud-upgrade-v1.2.1.zip`) and `custom textures.zip` installer embedded directly into `acz_hdtex.dll` (replacing the standalone `.bat` script).

* **v1 – v4:** Cropping aircraft icons (`384x384 -> 288x288`), weapon icons (`256x256`), squadron emblems (16:9 to 4:3 correction), generation of multi-hash `.json` files, assembly of 36 weapon atlases (`2048x1024`), PS2-to-PC alpha correction (`* 255 / 128`), and generation of 60 hangar weapon `TIM2` textures (`320x320`).

* **v5 – v6:** Separation of hangar assets into categorized subdirectories; fixed combat HUD atlas source `ux\0.png` (`d00645d18395b834`) with zero-alpha normalization to `RGB=(105, 105, 105)`.

* **v7 – v8:** Assembly of 8x HD `MASTER_EN_font_64x64.png` (`8192x1024`, 23 letterhead glyphs) and 4x HD `MASTER_EN_font_32x32.png` (`2048x5632`, `128x128` tiles, 75% memory reduction) with unified `16x16` glyph definitions in `MASTER_EN_font_32x32.json`.

* **v8 – v8.1:** Full support for `custom textures.zip` (processing `For OstrichMods\` and deploying `textures\` to `mods\textures\`), integration of 38 squadron intro glyphs (`#601..#638`), generation of `subtitle_brackets_BB10DED7AB6393C6.png`, optimized PNG compression (`compress_level=4`), and Zero-Install Portable Python Mode.

### 2.6. Python Utility Suite (`py\`)

* **`py\ACZ_Resource_Scanner.py` (v9):** Scans extracted `.arc` / `.fhm` containers, clusters font families (`FAMILY_*`), merges language slots (`1us` / `2uk` into `EN`), and builds unified master atlases (`MASTER_*`) with standard baseline alignment (`std_h`).

* **`py\ACZ_TIM2_Scanner.py` (v1):** Universal scanner for `TIM2` assets in `.swa` (`SWAD`), `.fhm`, `.msa`, `.bin`, `.dat`, `.tm2`; decodes 4-bit, 8-bit (`CSM1`), 16/24/32-bit textures with PS2 alpha scaling and triple-hash calculation for `acz_hdtex.dll`.

* **`py\ACZ_FHM_Converter.py` (v5):** Unpacker/packer for nested `.FHM` archives and font metric tables (`*Pmenu.fhm`) across all 7 language slots, with batch export for `acz_hdtex.dll`.

* **`py\ACZ_GIM_PNG_Converter.py` (v5):** Two-way converter for 4-bit (`PSMT4`) and 8-bit (`PSMT8`) `.gim` textures to 32-bit RGBA PNGs with `CSM1` descrambling and 64-bit `FNV-1a` naming.

* **`py\ACZ_SWAD_Converter.py` (v1.0.1):** Unpacker/packer and automated 16:9 / 21:9 widescreen layout generator for `.swa` (`SWAD` / `SWG`) menu archives.

* **`py\ACZ_SWG_Visual_Editor.py` (v1):** Interactive WYSIWYG GUI editor for `.swa` / `.swg` menu layouts, timelines, and matrix transformations (`4:3`, `16:9`, `21:9`).

* **`py\ACZ_Terrain_Converter.py` (v1.0):** Converts PS2/PC terrain containers (`mXX.mtd`, `.mtc`, `.mti`, `.mta`) into seamless context-padded `512x512` sheets (`6x6` core + `1`-tile neighbor border) for `4x` AI upscaling (`2048x2048`) and slices upscaled sheets back into individual `256x256` FNV-1a hashed tiles for `acz_hdtex.asi`.
