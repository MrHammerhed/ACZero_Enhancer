# Widescreen Menu Architecture (`16:9` / `21:9`), Textures & Controller Glyphs in Ace Combat Zero (PC Port)

This document summarizes the reverse-engineering findings for `acz.exe` (`v1.0.2.1` and `v2.0.0.1`), localization archive `select.msa`, and `.swa` (`SWAD` / `SWG`) menu containers.

---

## 1. Screen Format Option & Dynamic 21:9 Support

When switching `SCREEN SIZE` in `OPTIONS -> DISPLAY SETTINGS`, the game updates the aspect option in `CSysData` (`0 = 4:3`, `1 = 16:9`, and with `enhancer.dll` `ApplyEngine21x9AspectPatches`, `2 = 21:9`).
- `enhancer.dll` extends the `Display Settings` menu state machine (`opt_sRatio`) to support three selectable formats (`4:3`, `16:9`, `21:9`), persists `21:9` in the save file without breaking vanilla save compatibility, and dynamically updates the 3D and 2D projection matrices whenever the aspect ratio changes.

---

## 2. D3D12 Scissor Pillarbox Control (`UpdateGsFrameAndScissor`)

In `UpdateGsFrameAndScissor` (`0x140194380..0x140194500` in `v1.0.2.1` / `0x140194A00` in `v2.0.0.1`), two conditional branches restrict the D3D12 scissor rectangle to a centered 4:3 box (`1440x1080` at `1920x1080`):
- **`RVA_ScissorBranch1`**: `0x001943C4` (`v1.0.2.1`) / `0x00194A44` (`v2.0.0.1`) — `74 17` (`je +0x17`)
- **`RVA_ScissorBranch2`**: `0x001944BB` (`v1.0.2.1`) / `0x00194B3B` (`v2.0.0.1`) — `75 35` (`jne +0x35`)

### Scoped Widescreen Scissor Override (`SetWidescreenMenuScissorOverride`)
To ensure unmodified 4:3 menus retain clean 4:3 pillarbox borders while custom widescreen menus (`<Menu>_16x9.swa` / `<Menu>_21x9.swa`) render edge-to-edge:
- `Hook_CSwgInstanceInit`, `Hook_CSwgInstanceUpdate`, and `Hook_CSwadMenuDraw` inspect whether the active menu has a widescreen override (`g_HasWidescreenMenuOverride[stem]`) and check the active aspect tier (`GetTargetMenuAspectTier()`):
  - When a widescreen menu override (`_16x9` or `_21x9`) is active and `tier > 0`, `SetWidescreenMenuScissorOverride(true)` opens the scissor to the full viewport width (`0..width`).
  - When an unmodified 4:3 menu is active or the game is set to `4:3` (`tier == 0`), `SetWidescreenMenuScissorOverride(false)` restores exact 4:3 pillarboxing.

### Dynamic 4:3 Pillarbox Ratios on 21:9 Ultrawide Displays
In stock `acz.exe`, the 4:3 pillarbox multipliers were hardcoded for a 16:9 monitor (`left = 0.125f`, `right = 0.875f`, `scale_x = 0.75f`), which caused 4:3 pillarboxed content on a 21:9 monitor to stretch to 16:9.
- `UpdateDynamicPillarboxAndAspect` in `enhancer.cpp` redirects these constants to `RVA_PillarboxDataCave` (`0x00308F40`) and computes exact geometric ratios for the active resolution (e.g., for `21:9`: `scale = 4/7`, `left = 3/14`, `right = 11/14`), keeping 4:3 pillarboxed menus and briefing videos geometrically accurate on any display.

---

## 3. `.swa` (`SWAD`) Container & `SWG` Layout Architecture

Each menu archive in `data/select/*.swa` is a `SWAD` container:
- `+0x00`: `"SWAD"` signature (4 bytes)
- `+0x04`: `chunk_count` (`uint32_t`)
- `+0x08`: Array of `chunk_count` 8-byte descriptors (`uint32_t offset`, `uint32_t size`):
  - **Chunk 0 (`chunk00_layout.swg`)**: binary vector/sprite scene graph and `SWG\0` ActionScript bytecode.
  - **Chunk 1..N (`chunkNN_texture.tm2`)**: embedded `TIM2` textures (backgrounds, frames, icons, font glyphs).

### Key `SWG\0` Header Offsets
- `+0x00`: `"SWG\0"` (4 bytes) + `uint32_t crc`
- `+0x08..+0x47`: Menu stem name (`char[64]`, e.g. `"MainSelect"`)
- `+0x50`: `uint16_t canvas_width` (default `512` = `0x0200`; `682` for `16:9`; `896` for `21:9`)
- `+0x52`: `uint16_t canvas_height` (default `448` = `0x01C0`)
- `+0x54`: `uint32_t table0_offset`, `+0x58`: `uint32_t table0_count` — **Table 0 (Placement Matrices)**: array of 64-byte `4x4 float` transform matrices (`m00` at `+0x00`, `m11` at `+0x14`, `tx` at `+0x30`, `ty` at `+0x34`).
- `+0x5C`: `uint32_t table1_offset`, `+0x60`: `uint32_t table1_count` — **Table 1 (Color Transforms)**: RGBA multipliers and offsets.
- `+0x64`: `uint32_t table2_offset`, `+0x68`: `uint32_t table2_count` — **Table 2 (Texture Atlas UV & Blend Descriptors)**: texture page dimensions (`512x448`, etc.) and PS2 GS blend mode flags.
- `+0x7C`: `uint32_t table5_offset`, `+0x80`: `uint32_t table5_count` — **Table 5 (Dictionary Entries)**: scene elements (`type == 0` quads/sprites, `type == 3` `TextField`, `type == 5` `MovieClip`).

---

## 4. Key `CSwgRenderer` Engine Behaviors

### 4.1. Full-Screen Background Quads (`CSwgRenderer::DrawQuad` @ `0x1401EC520`)
`CSwgRenderer::DrawQuad` ignores `x0` (`roff + 20`) and `y0` (`roff + 24`) on `Type 0` records and always builds vertices from `(0, 0)` to `(width, height)` (`roff + 28`, `roff + 32`), multiplying them by `Table 0` matrix `mat_idx = *(int32_t*)(roff + 8)`.
- To span the full widescreen canvas (`-85.3333..+597.3333` in `16:9` or `-192.0..+704.0` in `21:9`), full-screen background quads are assigned a placement matrix with `tx = -85.333336f` (`16:9`) or `tx = -192.0f` (`21:9`) and `width = 682.6667f` / `896.0f`.

### 4.2. ActionScript `zz_setMoveXY` Button Glyph Overrides (`0x1401F6F90`)
In `MainSelect`, ActionScript bytecode calls `zz_setMoveXY(169, ...)` (`0x1401F6F90`) with language-specific X offsets for button glyphs. In `DictType[0] handler` (`0x1401FB6A0`), any non-zero `zz_setMoveXY` offset overrides `Table 0` `tx`, whereas `0` instructs the engine to read `tx` directly from `Table 0`. Zeroing the hardcoded script override allows each aspect variant (`4:3`, `16:9`, `21:9`) to position prompt glyphs cleanly via its own `Table 0` matrix.

### 4.3. Real-Time Aspect Switching via `CSwgInstance::Update` and `CSwadMenu::Draw`
- **`CSwgInstance::Update`** (`RVA 0x001F8180` in `v1.0.2.1` / `0x001F8800` in `v2.0.0.1`) runs before every logic tick of any `CSwgInstance`, swaps `Table 0` / `Table 3` in memory when `SCREEN SIZE` changes, and refreshes world matrices (`mc + 0x140..0x17F`) across all active `CMovieClipInstance` nodes (`RefreshMovieClipWorldMatrices`).
- **`CSwadMenu::Draw`** (`RVA 0x001CBB70` in `v1.0.2.1` / `0x001CC1F0` in `v2.0.0.1`) runs every frame for top-level menus, synchronizing the root `CSwgInstance` (`swad_menu + 0x08`) and maintaining the appropriate widescreen scissor state even after returning from child `loadMovie` submenus.

---

## 5. Aspect-Ratio Suffixes (`_16x9` / `_21x9`) & Controller Glyph Packs (`pads/`)

### 5.1. Menu Archive Variants (`mods\textures\menus\<MenuName>\`)
- `mods\textures\menus\<MenuName>\<MenuName>_16x9.swa` — `16:9` layout (`682x448`)
- `mods\textures\menus\<MenuName>\<MenuName>_21x9.swa` — `21:9` layout (`896x448`)
- `mods\textures\menus\<MenuName>\<MenuName>.swa` — base `4:3` layout (`512x448`)

### 5.2. Texture Aspect Variants (`mods\textures\`)
For any replacement texture `<HASH>.png`:
- `<HASH>_16x9.png` is selected when `16:9` is active (or as fallback in `21:9` if no `_21x9.png` exists).
- `<HASH>_21x9.png` is selected with highest priority when `21:9` is active.
- `<HASH>.png` is used in `4:3` mode.

### 5.3. Controller & Keyboard Glyph Sets (`mods\textures\pads\`)
Configured via `GamepadGlyphs=AUTO` (`AUTO`, `PS`, `XBOX`, or `KB`) in `acz_hdtex.ini`:
- Dynamically selects PlayStation, Xbox, or Keyboard button prompts (`pad_*.json`) and Controls Manual diagrams based on live input activity.
