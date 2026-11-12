# ACZ SWG/SWAD Visual Menu Editor Guide (v2.1)

The visual editor **`py/ACZ_SWG_Visual_Editor.py`** lets you inspect, edit, adapt for widescreen displays (`16:9` / `21:9`), and translate any in-game menu in **Ace Combat Zero** (`.swa`, `.swg`, `select.msa`) in **WYSIWYG** mode.

---

## 1. Key Features in v2.1

1. **Accurate PS2 GS Additive Blending (`Table 2 blend_flag == 1`)**:
   - At offset `0x64` in the SWG header lies **Table 2** (`12` bytes per texture: `tex_id, wh_packed, blend_flag`).
   - Background spark layers (`Tex[2]`), cockpit silhouettes (`Tex[1]`), scanlines (`Tex[3]`), and frames (`Tex[0]`) use **`blend_flag = 1` (PS2 GS additive blending: `dst.rgb += src.rgb * alpha`)**, where the black `(0, 0, 0)` backdrop around sparks has alpha `128` (`255`) and adds `+0` without darkening underlying layers.
   - The editor implements exact additive compositing (`ImageChops.add`) driven by Table 2 flags so backgrounds, flares, and sparks match in-game rendering.
2. **Hold Frame Inheritance across Timeline Scrubbing (`opcnt == 0`)**:
   - In many menus (`Brightness.swa`, `SaveLoad.swa`, `Shop.swa`, `AircraftSelect.swa`), intermediate animation frames contain no new placement commands (`opcnt == 0` or only ActionScript bytecode `op == 6`), retaining the display list from the preceding keyframe.
   - The editor automatically inherits active element placements across hold frames so textures and buttons remain visible throughout timeline scrubbing.
3. **Smart `MainUI MovieClip` Selection & `MC:` Dropdown**:
   - Across 15 of the 31 `.swa` archives, the scanline overlay clip has `180` frames with only `1` child element, whereas the interactive menu clip has `37..175` frames and `10..53` child elements.
   - The editor ranks MovieClips by `(>= 3 elements, named frame labels, peak child count, frame count)` and provides an **`MC:`** dropdown in the top toolbar to switch between any MovieClip in the archive.
4. **Zoom (`50%..300%`) and Camera Panning**:
   - Cursor-centered mouse wheel zoom, **`-` / `+`** buttons, and a **`Zoom:`** preset selector (`50%` to `300%`).
   - Free camera panning by holding the **Right Mouse Button (RMB)** or **Middle Mouse Button (MMB)**.
5. **`select.msa` String & Glyph Pool Decoder/Repacker**:
   - Decodes the `SHT` hash table (Chunk 0) and all 8 font glyph pools (Chunks 1..8) from `select.msa`, rendering actual in-game strings (`MAIN MENU > OPTIONS > SCREEN SETTINGS`, `SCREEN POSITION`, `BRIGHTNESS`, `OK / CANCEL`, etc.) directly on the canvas.

---

## 2. Requirements and Usage

Requires **Python 3.8+** and **Pillow** (`PIL`):

```bash
pip install pillow
python py/ACZ_SWG_Visual_Editor.py
```

You can also pass a `.swa` or `.swg` path directly on launch:

```bash
python py/ACZ_SWG_Visual_Editor.py mods/textures/menus/MainSelect/MainSelect_16x9.swa
```

On startup, the editor automatically looks for `mods/textures/menus/select.msa`. You can also load any `select.msa` manually via the **`Load select.msa`** toolbar button.

---

## 3. Toolbar & Canvas Controls

### Top Toolbar
* **`Open .swa / .swg`** — Open a `.swa` menu archive (with embedded `TIM2` textures) or a standalone `.swg` layout file.
* **`Save .swa / .swg`** — Save changes back to `.swa` (automatically repacking the `SWG` chunk and all `TIM2` textures with 64-byte alignment) or `.swg`.
* **`Load select.msa`** — Load a `select.msa` string archive to display and edit in-game menu text.
* **`Viewport:`** — Switch the virtual canvas width stored in the SWG header (`+0x50`):
  * `4:3 (512x448)` — standard PS2 4:3 canvas (`512x448`);
  * `16:9 (682x448)` — widescreen 16:9 canvas (`682x448`, `85px` side margins around the `512px` center frame);
  * `21:9 (896x448)` — ultrawide 21:9 canvas (`896x448`, `192px` side margins).
* **`Auto 16:9` / `Auto 21:9`** — Automatically recalculate placement matrices (`Table 0`) and background quads (`Dict Type 0`) for `16:9` or `21:9` in one click.
* **`MC:` (MovieClip Selector)** — Select which `Type 5 MovieClip` to inspect and edit (`[MainUI]` selected by default).
* **`Label:` (State Label)** — Jump directly to named menu states (e.g., `main`, `cmp_main`, `opt_main`, `opt_disSet`, `opt_sRatio`).
* **`Zoom:` (`-`, `100%`, `+`, `Reset View`)** — Adjust canvas zoom (`50%`–`300%`) or reset camera position.
* **`Textures` / `Boxes`** — Toggle texture/text rendering and wireframe bounding boxes.

### Mouse Controls on Canvas
1. **Zoom**: Scroll the **Mouse Wheel** up/down over the cursor position, or use **`-` / `+`** and **`Zoom:`**.
2. **Pan**: Hold **RMB** or **MMB** and drag the canvas. Click **`Reset View`** to recenter.
3. **Select & Move (`tx, ty`)**: Left-click (**LMB**) any element on the canvas (or select it in the **Scene & Transform** list on the right) and drag inside the yellow selection box to update `tx, ty` in `Table 0` in real time.
4. **8-Handle Resize Gizmo**: Drag the 8 yellow handles (`W`, `E`, `N`, `S`, `NW`, `NE`, `SW`, `SE`) on the selection border to scale width (`m00`) or height (`m11`) while anchoring the opposite edge, or enter exact numeric values in the **Transform & Edge Resize Inspector** panel.

---

## 4. Step-by-Step Widescreen Menu Adaptation (`16:9` / `21:9`)

1. Open an original `.swa` archive from `d.arc` (for example, `Brightness.swa` or `AircraftSelect.swa`) via **`Open .swa / .swg`**.
2. Click **`Auto 16:9`** (or **`Auto 21:9`**) on the top toolbar to expand the canvas to `682x448` (`896x448`), stretch full-screen backdrops, and shift left/right anchored elements.
3. Step through the menu states using the **`Label:`** dropdown and **`Timeline Frame`** slider, fine-tuning any individual element positions with the mouse or transform inspector.
4. Click **`Save .swa / .swg`** and save the file to `mods/textures/menus/<MenuName>/`:
   - `mods/textures/menus/<MenuName>/<MenuName>_16x9.swa` for `16:9`;
   - `mods/textures/menus/<MenuName>/<MenuName>_21x9.swa` for `21:9`.

---

## 5. Editing Menu Text (`select.msa`)

1. Open the **`Text & MSA Editor`** tab in the right-hand panel.
2. Filter keys using **`Filter MSA Keys:`** (for example, `SCREEN RATIO` or `H00000_036`).
3. Select an entry, edit the text in **`Edit Selected String`**, and click **`Update String & Preview`** to preview the change immediately on the canvas.
4. Click **`Save select.msa...`** to export the updated `select.msa`.
