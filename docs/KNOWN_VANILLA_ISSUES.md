# Known Vanilla Map & Terrain Issues (Ace Combat Zero)

Track vanilla asset quirks and bugs discovered during HD terrain upscaling so they can be documented in changelogs or optionally patched in `ACZero Enhancer`.

---

## `m01a` (Mission 01 — Glacial Skies)

### Sector `(11, 3)` Texture Grid Mismatch in `m01a.mta`
- **File**: `extracted\a\data\map\m01a\m01a.mta` (offset `0x3B`, row `3`, col `11`).
- **Symptom**: The `16x16` coarse texture sector grid (`m01a.mta`) repeats a seamless `2x2` pattern of sectors (`0, 1` on even rows; `2, 3` on odd rows) across the entire map, **except** at cell `(11, 3)` where the value is `2` instead of `3`. This creates a sharp rectangular texture seam around world tiles `x=176..191, y=48..63`.
- **Is the polygon mesh affected?**: **No.** Inspection of `m01a.mha` (heightmap sector grid) shows that the `3x3` block centered on `(11, 3)` (`x=10..12, y=2..4`) intentionally uses unique sculpted heightmap sectors `11..20` (likely the flattened Valais Air Base valley), while the surrounding map repeats base height sectors `5, 6 / 9, 10`.
- **Likely cause**: Either a manual developer typo in `m01a.mta`, or a quick override to place the brighter snow corner of sector `2` underneath the flattened `3x3` base area without authoring a dedicated seamless transition sector.
- **HD Terrain Export Handling**: Exported `m01a_sheets` using a clean `m01a.mta` (`sector(11, 3) = 3`) so all 1024 unique tiles are upscaled with proper seamless neighbor context (`25` full `7x7` sheets, zero `quad` sheets, zero seams).
- **Future Changelog / Potential Fix**: Verify in-game at world sector `(11, 3)` whether patching `m01a.mta[0x3B]` from `2` to `3` looks cleaner or exposes rocky texture under Valais Air Base; document any fix in the release changelog.

---

## `m03k` (Mission 03 — The Round Table / Area B7R; shared by `m03m`, `m03n`, `r01a`, `v05a`)

### Harsh Black-Speckle Scrubland Pattern & `64x64` Tile Seams
- **Files**: `extracted\a\data\map\m03k\m03k.mtd` / `m03k.mti` (dense scrubland zones, notably visible on exported sheets `002`, `003`, `014`, `015`, `018`, `024`, `026`, `030`, `032`).
- **Symptom**: Stock PS2 scrubland terrain tiles use an extremely high-contrast coal-black-on-tan speckled noise pattern with visible `64x64` tile repetition seams (especially on `m03k_sheet_024`). When upscaled directly to 4x HD, the pattern reads like harsh black mold/soot underneath the game's muted olive-grey 3D bushes.
- **HD Terrain Fix & Changelog Note**: Replaced the harsh black speckles and stock `64x64` grid seams across the `296` scrubland tiles with a unified seamless muted olive-grey-brown desert scrubland texture (`--scrub-lock`, sourced from `m03k_sheet_015`), matching the in-game 3D bushes, and applied a gentle `1 px` edge-preserving guided denoise across all `1003` tiles to tame high-frequency sand shimmer.
- **Future Polish TODO**: Eliminate subtle `256x256` periodic tiling repetition and refine the scrubland fill blending (e.g., via world-space macro-variation / noise breakup in the future custom D3D12 terrain shader, or denser 3D scrubland placement via the future Map Editor).

---

## `m04k` (Mission 04 — Juggernaut / Futuro Canal; shared by `m04m`, `m04n`)

### Vanilla Heightmap Relief Limitations & Bridge Shadow
- **Files**: `extracted\a\data\map\m04k\m04k.mha` / `m04k.mhd` / `m04k.mtd`.
- **Symptom**: Even with clean 4x HD textures (`890/890` tiles), the stock low-resolution heightmap geometry around Futuro Canal and the coastal banks limits visual fidelity in-game. Additionally, the baked bridge shadow across the narrow canal neck (`m04k_sheet_011`) does not upscale cleanly via AI alone.
- **HD Terrain Post-Processing**: Flattened the repeating deep-ocean tile (`185E88C8B8EB6325.png`, used 16,185 times) to its uniform median RGB `[70, 91, 100]` to remove a corner glow artifact.
- **Future Polish TODO**: Rework/refine the `m04k` terrain heightmap (`mha`/`mhd`) once the Map Editor / heightmap pipeline is implemented.

---

## `m06a` (Mission 06 — Diapason / Directus)

### Stair-Stepped `64x64` River Water Alpha Mask
- **Files**: `extracted\a\data\map\m06a\m06a.mtd` / `DrawMesh_p.cso` (`alpha < 0x80` water branch).
- **Symptom**: Land, fields, and forests look clean in 4x HD (`1003/1003` tiles), but the narrow winding river appears flat and stair-stepped at low altitude (`ALT ~300`). This is caused by the stock `64x64` binary water alpha mask (`< 0x80`) being preserved directly in `acz_hdtex.asi` without higher-resolution shoreline smoothing, combined with the stock flat river shader color.
- **Future Polish TODO**: Generate smooth high-resolution (`256x256` / `512x512`) river alpha masks aligned to the HD shoreline and/or add water normal ripples + soft shoreline blending in the custom D3D12 terrain shader.



