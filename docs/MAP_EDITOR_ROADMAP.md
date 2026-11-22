# Ace Combat Zero — Custom Map Objects & Visual Map Editor Roadmap

Design notes and feasibility plan for adding custom map objects (trees, buildings, props), building a visual 2D/3D map editor, and integrating semi-automated object detection from HD terrain textures.

---

## 1. Vanilla Map Architecture (`extracted\a\data\map\<map>\`)

Each gameplay map consists of compact, modular binary tables (verified on `m02a`):

| Layer | Files | Approx. Size (`m02a`) | Role |
| :--- | :--- | :---: | :--- |
| **Heightmap** | `<map>.mha`, `<map>.mhd` | 256 B / 17 KB | `16x16` coarse height sector grid + vertex height data |
| **Terrain Textures** | `<map>.mta`, `.mti`, `.mtd`, `.mtc` | ~5 MB | `16x16` texture sector grid, `256x256` tile index map, 64x64 `PSMT8` tiles + CLUT *(already decoded & HD-replaced via `acz_hdtex.asi`)* |
| **Trees / Forests** | `<map>_tree1..3.acm`, `.gim`, `<map>.efa` | 2 KB each / 16 KB | Tree 3D models, textures, and `16,384 B` (`128x128`) forest placement/density mask |
| **Placed Map Objects** | `pmmdl<map>.bin`, `pmtex<map>.bin` | 160 KB / 61 KB | Static 3D model library (buildings, bridges, base structures) and textures |
| **Object Placement** | `<map>.pml`, `<map>.pdl`, `<map>.pmi` | 1.7 KB / 1.7 KB / 128 KB | Instance transforms/IDs and `256x256 x uint16` (`131,072 B`) spatial lookup grid |
| **Water / Bounds / Waypoints** | `<map>.wad`, `.wbd`, `.wpd`, `*_sea.gim` | 5–27 KB | Auxiliary geometry, collision/boundary data, water shaders |

---

## 2. Implementation Phases

### Phase 1 — Existing Object & Tree Placement (High Feasibility, Low Risk)
- **Goal**: Reverse-engineer `.pml`, `.pdl`, `.pmi` (position, yaw/rotation, model ID) and `.efa` (tree placement grid).
- **Capabilities**:
  - Add, move, rotate, or remove buildings, bridges, base props, and tree clusters using existing models from `pmmdl<map>.bin` (or models transplanted from other ACZ maps).
  - Verify `acz.exe` runtime limits (max instances per sector / per frame) and expand buffers via `.asi` hook if needed.
- **First Proof-of-Concept**: Place one extra building and one new forest patch on `m02a`, load via file-override hook in `.asi`, and verify in-game with `F2` screenshots.

### Phase 2 — Visual 2D/3D Map Editor (High Feasibility, Medium Effort)
- **Architecture**: Local web app (**Python backend + Three.js / WebGL frontend**) or desktop app (**Python + PyQt/OpenGL**).
- **Core Features**:
  - 3D viewport built directly from `mha/mhd` heightmap + HD terrain tiles (`256x256`) + synchronized 2D top-down satellite view.
  - Mouse-driven object placement (select, translate, rotate, snap to terrain height) and forest brush painting for `.efa`.
  - Non-destructive export of patched map files to `mods\maps\<map>\`, loaded dynamically by `.asi` without repacking game archives.

### Phase 3 — Custom 3D Model Import (Medium-High Feasibility, Higher Effort)
- **Goal**: Build a `.glb / .obj` -> `.acm` / `pmmdl*.bin` converter (vertices, normals, UVs, materials, LODs) with HD texture replacement via `acz_hdtex.asi`.
- **Key Technical Challenges**:
  1. **Collisions**: Locate and generate bounding boxes / collision geometry (`.pdl`, `.wbd`, or embedded in `pmmdl`) so aircraft collide accurately with new structures.
  2. **Engine Buffer Limits**: Check vertex/index pool caps in `acz.exe` and hook/expand if required.
  3. **Ground Shadows**: Because ACZ relies heavily on baked terrain shadows (e.g., under viaduct bridges), large custom structures should optionally bake soft ambient shadows into the underlying HD terrain tiles.

---

## 3. Semi-Automated Object Detection from HD Textures

Rather than blindly auto-populating maps, the editor should include an interactive **"Detect Buildings / Forests in Selection"** assistant that generates editable draft candidates.

### Detection Targets
1. **Forests & Trees (~90–95% accuracy)**:
   - Color/texture segmentation of forest canopies on HD terrain tiles (`256x256`).
   - Automatically masked against stock water alpha (`< 128`), `--road-lock` highway masks, and steep cliff slopes from `mha/mhd`.
2. **Villages & Urban Buildings (~75–90% accuracy)**:
   - On 4x upscaled HD tiles, roof footprints (e.g., terracotta roofs in `m02a` villages or dense city blocks in `m06a`/`m11a`) have crisp rectangular boundaries.
   - Compute Oriented Bounding Boxes (`OBB`: center `x, y`, width `w`, length `h`, rotation `angle`) via classical CV (`minAreaRect` / color & edge masks in NumPy/OpenCV) for simple rural maps, with an optional local satellite-building OBB neural detector for dense urban maps.
   - Automatically match footprint dimensions `(w, h)` to the closest building prefab in `pmmdl` and snap elevation `z` to `mha/mhd`.

### Mandatory Safety Filters
1. **Exclusion Zones**: Skip detections overlapping existing `.pml` 3D objects, mission targets, bridges, roads, or water (`alpha < 128`).
2. **Slope Threshold**: Reject building placements on terrain slopes steeper than ~10–15 degrees (prevents floating foundation corners).
3. **Playable Area & Instance Budget**: Restrict auto-population to the active combat area so repeated boundary tiles do not exhaust engine instance limits or FPS.
4. **Footprint Coverage**: Prefer 3D building prefabs slightly larger (`+5–10%`) than the detected painted roof (or softly tone down the underlying tile footprint) so flat painted roofs do not peek out from beneath 3D walls.

### Recommended Editor UX Workflow
1. User drags a selection box over a village or wooded hill in the editor.
2. User clicks **"Auto-Detect Buildings"** or **"Auto-Paint Forest"**.
3. Detector populates **draft candidate objects** (visually highlighted).
4. User spends 10–20 seconds deleting false positives or tweaking rotations, then clicks **"Commit"**.

This hybrid workflow delivers a **10–20x speedup** over manual placement while preserving full visual quality control.

---

## Future Graphics Extension: Terrain Normal Maps & Custom D3D12 Scene Shading

### Motivation
- The stock ACZ engine applies dynamic shadows only to aircraft; terrain relief relies entirely on shadows baked into low-resolution PS2 albedo tiles (`*.mtd`).
- Because terrain tiles repeat across different 3D hills and flat valleys (e.g., `m01a` repeating `32x32` sectors), baked mountain shadows inevitably clash with actual 3D geometry and become much more noticeable after HD upscaling.
- Removing baked shadows completely from albedo textures without a replacement lighting model would make mountains look flat.

### Planned Long-Term Architecture (Paired with Map Editor / Renderer Mod)
1. **World-Space Normal & Height Maps from `mXX.mha` / `mXX.mhd`**:
   - Compute accurate world-space terrain normals (and optional macro self-shadowing / ambient occlusion) directly from the map's heightmap (`mha`/`mhd`) rather than relying on tiled albedo shading.
2. **Per-Tile PBR Maps (Detail Normal + Roughness) from Archived `2048x2048` Upscale Sheets**:
   - Generate seamless tangent-space detail normal maps (`256x256` `BC5_UNORM`) and roughness/glossiness maps directly from the full `2048x2048` upscaled sheets (prior to slicing, using the `0.5`-tile context border so tile edges stay 100% seamless).
   - **Hybrid AI + Signal-Processing Pipeline**:
     - Run local monocular normal/depth estimation (`Depth Anything V2`, `Marigold-Normals`, or `DSINE`) on the upscaled sheets to extract semantically accurate mid-scale relief (ravines, ridges, rock strata) without false steps on flat color/field boundaries.
     - Blend with high-pass multi-scale Sobel/Scharr gradients for crisp micro-grain detail (rock cracks, scree, dry washes).
     - Derive paired **Roughness / Specular** maps (plus optional stock water-alpha mask `< 128`) so rivers, lakes, ice, and wet runways produce physically plausible sun glints while rock, soil, and sand remain matte.
   - **Archival Rule**: Always retain `work\<map>_upscaled\` and `work\<map>_sheets\manifest.json` so PBR maps can be batch-generated offline for every completed map without re-running Nano Banana.
3. **Custom D3D12 Terrain & Scene PBR Shading Shader**:
   - Bind the paired `BC5` detail normal (and roughness) `Texture2DArray` alongside the HD albedo `Texture2DArray` in `acz_hdtex.asi`.
   - Hook/replace the terrain pixel shader (and eventually scene lighting passes) to combine **macro world-space normals** (`mha/mhd`) with **micro detail normals** (RNM / Whiteout blending), evaluating directional sunlight, slope-accurate mountain self-shadowing, specular/roughness response, and ambient sky fill in real time.
   - Once real-time PBR terrain shading is active, terrain albedo textures can be authored as pure, shadow-free (`de-lit`) albedo maps without losing 3D relief depth.

---

## Future Terrain Pass 2: `512x512` Clean Albedo (Draft Ideas — Validate by Prototype)

> Status: idea collection only. Every point below must be validated on a small prototype (e.g. one `7x7` area of `m05a`) and compared in-game before committing to full-map production. The Pass 1 (`256x256`) HD pack remains the shipping baseline.

### Motivation
- At low altitude, `256x256` tiles may still look soft; AI-invented relief in Pass 1 albedo does not always match the real `mha/mhd` geometry.
- Painted trees, buildings, and baked shadows in albedo clash with real 3D objects and with future dynamic lighting.

### Step 0 — Measure Before Producing
- Add a debug mip-level colorization mode to `acz_hdtex.asi` and fly at typical low altitudes. Only proceed with `512x512` if mip 0 is frequently magnified on screen.

### Hybrid Input Generation (Avoid AI-on-AI Drift)
- Per tile: vanilla `64x64` upscaled 2x + finished Pass 1 `256x256` downscaled 2x -> `128x128` hybrid tile, combined as **luminance/detail from Pass 1 + chroma/base tone from vanilla** (or a simple 50/50 blend as a fallback).
- Export sheets of `4x4` tiles (`3x3` core + `0.5`-tile border) -> `512x512` input -> Nano Banana `2048x2048` -> `512x512` per tile (about 4x the sheet count of Pass 1).
- In the same pass, prompt the model to remove baked shadows, painted trees, and buildings (bare ground / foundations / forest floor).
- Compare against a fully local alternative (Real-ESRGAN / SwinIR 2x over archived `work\<map>_upscaled\` 2K sheets): zero manual work, no hallucination.
- Optionally condition inputs with heightmap-derived hillshade / slope masks (`mha/mhd`) so rock, scree, and snow placement follows the real geometry.

### Gameplay-Corridor ROI Scoping & Tile Deduplication (Reduce Pass 2 Workload)
- Because Pass 1 (`256x256`) already covers 100% of every map as the baseline, Pass 2 (`512x512`) does **not** need to cover the entire map:
  - **Gameplay-Corridor ROI**: Scope Pass 2 exports (`--roi` / mask in `ACZ_Terrain_Converter.py`) to the actual **gameplay corridor** where mission design keeps the player at low altitude (spawn -> targets -> active combat area, e.g. city center, canyon, dam, airbases). Non-corridor tiles ("rear" areas only seen on turnarounds or in Free Flight) seamlessly fall back to Pass 1 (`256x256`), where the `512 -> 256` transition is virtually invisible at altitude. Expected reduction: **~50–70%** on linear/targeted missions (`m06a`, `m09a`, `m15a`, `m17a`) and **~20–40%** on wide-area / 360-degree dogfight maps.
  - **Whole-Tile Near-Duplicate Deduplication (`--dedup-mae`)**: Project Aces frequently reused the same base `64x64` forest/field/mountain templates with minor baked lighting/quantization differences (different SHA-256 hashes, but `MAE < 2.5–3.0`). Deduplicating them at export time (storing aliases in `manifest.json` and expanding them at slice time with per-tile color-matching) cuts another **~30–50%** of tiles (useful in both Pass 1 and Pass 2).
  - **Partial-Tile Base-Layer Reuse**: Tiles where 70–80% is the `00` background sector and 20–30% is a painted road/river/base still require upscaling so transitions stay seamless, but in Pass 2 the already-upscaled `512x512` `00` background serves as a unified base layer for manual editing and masking.
  - **Combined Pass 2 Impact**: Together, ROI scoping + `--dedup-mae` reduce a typical `~120–160`-sheet Pass 2 map down to **~25–45 sheets** (**~70–80%** total workload reduction).

### Distance Fallback for Removed Forests
- 3D tree density is low in ACZ, so distant forests rely on painted canopy. Keep the Pass 1 (painted) look for distant terrain:
  - **Mixed mip chain**: mips `0..N` from clean Pass 2 albedo, mips `> N` from Pass 1 (full) albedo in the same `Texture2DArray` (zero extra VRAM). Trilinear filtering smooths the transition across one mip level; note that mip selection depends on screen-space derivatives (viewing angle, anisotropic filtering), not pure distance.
  - **Alternative**: explicit distance-based blend between two arrays in the custom terrain shader, tuned to match the 3D tree draw/fade distance.

### Object Masks (Manual-First)
- Object/forest placement masks are authored **manually** in the Map Editor. Automatic results (`|Pass1 - Pass2|` difference, color/texture segmentation, or AI-generated masks) are used only as **hints/overlays**, never applied directly (two independent AI generations differ almost everywhere).

### Shader & Albedo Co-Development
- Clean (de-lit) albedo and the custom D3D12 terrain lighting shader are developed together and iterated jointly; shadow-free albedo is never shipped without the matching lighting shader.

### PBR Map Options (To Be Evaluated in Practice)
- Store Normal / Roughness at `256x256` (`BC5` / `BC4`) generated from clean `512x512` albedo (supersampled down to reduce noise).
- Candidate approaches to compare by actual results:
  - per-tile AI + Sobel detail normals (see section above);
  - macro normals from `mha/mhd` + a small set of seamless tiling detail materials (rock, snow, grass, sand, forest floor) blended via a material/splat mask.
- Estimated VRAM per map: `256 MB` (`512x512x1024 BC7` albedo) + `64 MB` (`BC5` normal) + `32-64 MB` (roughness/AO) ~ `350-385 MB`. `acz_hdtex.asi` must upload the larger array in chunks or via a dedicated upload buffer (current upload ring slice is `128 MB`).

---

## Alternative / Preferred Architecture: Hybrid PBR Material Splatting + Man-Made Overlay

Instead of AI-upscaling natural terrain to `512x512` per tile, render natural ground in the custom D3D12 terrain shader using a **compact Material ID map + shared PBR tileable materials**, while keeping **Pass 1 (`256x256`)** as the macro tint and distance fallback:

1. **Integer Material ID Map (No Hardware Bilinear Artifacts)**:
   - Store material IDs per texel in a raw integer texture (`R8_UINT` or 4-bit packed `uint` for up to 16 materials per map — only **4 MB VRAM** at `64x64x1024` or **16 MB** at `128x128x1024`).
   - Never use hardware bilinear filtering on IDs. Instead, `Load()` the **4 corner texels** around `uv` in the pixel shader and smoothly blend the sampled PBR material outputs using `frac(uv)` + **Height-Based Blending** (each material provides `Albedo + Normal + Roughness + Height`).
   - Fast path: when all 4 corner IDs match (~90% of pixels), sample only **1 material** instead of 4.
2. **Fractal Anti-Tiling**:
   - Break up repetition on large plains/deserts (`m03k`, `m07a`) using **procedural fractal noise (FBM) UV/scale blending** (e.g., two rotated scales blended by FBM) or **stochastic hex-tiling** in the pixel shader.
   - Gives true aircraft-relative physical scale at `ALT 50–300` (fixing the PS2 oversized-ground-detail feel) and real PBR `Normal/Roughness` without AI normal noise.
3. **Pass 1 (`256x256`) as Macro Tint + High-Altitude / Distance Fallback**:
   - Close range: subtle macro color tint from Pass 1 preserves regional color variation and per-map mood.
   - High altitude / far distance: smoothly crossfade to our **Pass 1 (`256x256`) HD Texture2DArray**, preserving painted distant forests, fields, and macro landmarks with zero tiling.
4. **Man-Made Structures as an Overlay Layer (Never Cutouts)**:
   - The natural material ID map stays **100% continuous underneath** (never cut holes under roads, airbases, or cities — prevents missing-material fringes and visual bugs).
   - Roads, runways, aprons, cities, fortresses, and piers are composited **on top as an RGBA overlay layer** (smooth alpha edge blending over the live natural terrain below, or vector spline/polygon decals in the Map Editor). Pass 2 `512x512` baked textures are then only ever needed for these small man-made zones.
5. **Surface Micro-Relief (`POM`)**:
   - Distance-faded Parallax Occlusion Mapping (reusing the existing PBR `Height` channels) is **deferred** until the base PBR splatting + lighting shader is working and validated.

---

## Future Terrain Geometry: HD Heightmap, CDLOD, Analytical Erosion & Bathymetry

Vanilla `mha`/`mhd` (~17 KB per map) has very coarse PS2 polygon spacing (~200–400 m/vertex). Planned replacement pipeline:

1. **CDLOD / Geometry Clipmap Renderer + `dev` Draw-Distance Synergy**:
   - Keep the `dev` branch draw-distance work for engine-wide systems (`far clip` plane, fog distance, 3D object / tree / unit culling).
   - Replace the vanilla terrain mesh draw calls in `.asi` with a unified **CDLOD / Clipmap** grid reading a precomputed **`Heightmap_HD`** (`2048x2048` `R16_UNORM`, 8 MB VRAM). Eliminates chunk-seam cracks, fixes vanilla heightmap glitches (e.g. `m04k`), and scales cheaply out to extended far-clip distances.
2. **Zero-Readback Collisions (`Dual RAM + VRAM` Static Heightmap)**:
   - Never read height data back from GPU to CPU. Because `Heightmap_HD` is static and offline-baked (only 8 MB), load it once into **both VRAM (GPU rendering) and RAM (CPU queries)**.
   - Phase 1: keep moderate vertex displacement so vanilla collisions remain accurate.
   - Phase 2: hook `GetTerrainHeight(x, z)` in `acz.exe` to bilinearly sample the **RAM copy** (~5 ns, zero GPU-CPU stall) for exact aircraft/unit/explosion alignment.
3. **Gradual Analytical Noise Erosion + Manual Brush**:
   - Enhance upscaled `mha/mhd` via fast analytical/slope-directed noise erosion (not heavy hydraulic simulation), applied with a **smooth strength gradient** rather than a hard binary cutoff:
     - **~50–70%** strength on mountain slopes/ridges (experimental, tunable per map);
     - **~5–20%** subtle micro-undulation on plains/fields;
     - **Strict `0%`** mask on water shorelines, runways, roads, and building footprints, plus an interactive **manual erosion-strength brush** in the Map Editor.
   - Split amplitudes: conservative displacement in vertex geometry (preserves collision feel) + full strength in `World Normal` / `AO` and as hints for PBR material splatting.
4. **Automatic Object Grounding (`Z` Re-Snapping & Foundation Offset)**:
   - Whenever `Heightmap_HD` changes, automatically recalculate static `.pml` object elevations against the new surface (e.g., minimum corner height of the object's footprint minus an adjustable **foundation sink offset** so buildings never float on slopes; bridges/elevated structures tagged as excluded).
5. **Underwater Terrain / Bathymetry (For Future Realistic Water Shader)**:
   - A physically based water shader (depth-based absorption, shallow coastal transparency, foam, refraction) requires a real submerged seabed (`Z < 0`) rather than a flat water plane.
   - Do **not** apply mountain erosion underwater. Instead, generate bathymetry via **inward distance transform** from the shoreline/water mask (gentle coastal shelf + smooth low-frequency sand/riverbed undulation; shallow profiles for narrow rivers like `m06a`/`m08k`, deeper basins for lakes and open ocean like `m04k`/`m14k`), with manual depth-brush adjustments in the Map Editor.



