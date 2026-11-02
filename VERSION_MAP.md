# ACZero PC Port — Binary Version & RVA Mapping Reference (`1.0.2.1` vs `2.0.0.1`)

This reference documents the exact binary layout, PE header identifiers, and piecewise Relative Virtual Address (RVA) shifts between the two known executable builds of the **Ace Combat Zero: The Belkan War** PC port (`acz.exe`).

All plugins (`acz_hdtex.dll`, `memdump.dll`, `enhancer.dll`) include `acz_version_map.h` and automatically resolve these offsets at runtime via `DetectAczExeVersion()` and `AczResolveRva()`.

---

## 1. Executable Identification (`acz.exe`)

| Property | Version `1.0.2.1` (Initial Bundle Release) | Version `2.0.0.1` (Standalone Steam Release) |
| :--- | :--- | :--- |
| **Distribution** | Bundled with *Ace Combat 8* (manually separated) | Official separated standalone build |
| **Steam AppID** | Shared bundle AppID | `5370600` |
| **File Version / Product Version** | `1.0.2.1` | `2.0.0.1` |
| **File Size** | `3,962,920` bytes | `4,046,008` bytes |
| **PE `TimeDateStamp`** | `0x6A9E70E0` | `0x6AC61130` |
| **PE `SizeOfImage`** | `0x0385D000` | `0x0389B000` |
| **PE `AddressOfEntryPoint`** | `0x00253FA0` (`.bind` SteamStub v3.1) | `0x0388C0B0` (`.bind` SteamStub v3.1) |
| **Unpacked OEP (`.text`)** | `0x0024B5B0` | `0x0024D550` (`+0x1FA0`) |

---

## 2. Section Table Comparison

| Section | `1.0.2.1` VirtualAddress | `1.0.2.1` VirtualSize | `2.0.0.1` VirtualAddress | `2.0.0.1` VirtualSize | Base Shift |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`.text`** | `0x00001000` | `0x00279324` | `0x00001000` | `0x0027B294` | Piecewise (`+0` .. `+0x1FA0`) |
| **`.rdata`** | `0x0027B000` | `0x00090CE8` | `0x0027D000` | `0x000913AC` | `+0x2000` .. `+0x26C0` |
| **`.data`** | `0x0030C000` | `0x034DD4E0` | `0x0030F000` | `0x03518500` | `+0x3000` (`<0x330000`) / `+0x3020` (`>=0x330000`) |
| **`.pdata`** | `0x037EA000` | `0x0001CA88` | `0x03828000` | `0x0001CBD8` | `+0x3E000` |
| **`.rsrc`** | `0x03807000` | `0x00043FA8` | `0x03845000` | `0x00043FA8` | `+0x3E000` |
| **`.reloc`** | `0x0384B000` | `0x00002B84` | `0x03889000` | `0x00002BC4` | `+0x3E000` |

---

## 3. Piecewise `.text` RVA Shift Map (`1.0.2.1` -> `2.0.0.1`)

Why did `.text` shift piecewise?
1. **`+0x0010` at `0x004CA0`**: A small 16-byte adjustment in an early initialization helper.
2. **`+0x0040` at `0x004E80 .. 0x18AD1F`**: `48` bytes added at the end of `0x004CA0..0x004E7F`. Every single core PS2/mission/aircraft/camera/terrain/HUD function in `0x004E80 .. 0x18AD1F` is 100% byte-identical and shifted by exactly **`+0x0040`**.
3. **`+0x0680` at `0x18B800 .. 0x21E067`**: `2.0.0.1` inserted a new SteamAppId (`5370600`) / standalone launcher verification block at `0x18AD20 .. 0x18B7FF` (`+0x0640` additional bytes). Every single GS VRAM, GIM/TIM2/FHM parser, GameFont, and D3D12 renderer function in `0x18E520 .. 0x21E067` is 100% byte-identical and shifted by exactly **`+0x0680`**.
4. **`.arc` VFS (`0x222000 .. 0x226500`)**: `2.0.0.1` updated the `.arc` virtual filesystem to mount and read `c.arc` alongside `a.arc`, `b.arc`, and `d.arc`.

| `1.0.2.1` `.text` RVA Range | Delta (`2.0.0.1 - 1.0.2.1`) | Affected Subsystems |
| :--- | :---: | :--- |
| `0x00001000 .. 0x00004C9F` | `+0x0000` | Early CRT / entry helpers |
| `0x00004CA0 .. 0x00004E7F` | `+0x0010` | Init helpers |
| `0x00004E80 .. 0x0018AD1F` | **`+0x0040`** | Mission logic, camera, HUD, `BuildGimArrayGifPackets` (`0x14CA50 -> 0x14CA90`), terrain/sky/tree LOD & clip planes |
| `0x0018B800 .. 0x0018D7A3` | **`+0x0680`** | Early platform/window helpers |
| `0x0018D7A4 .. 0x0018E51F` | `+0x0685` | Window/input message loop |
| `0x0018E520 .. 0x0021E067` | **`+0x0680`** | **All GS VRAM, GIM, TIM2, FHM, GameFont, and D3D12 functions** (`acz_hdtex.dll` & `memdump.dll` `EndFrameAndPresent`) |

---

## 4. Complete Hook & Global Address Table (`1.0.2.1` vs `2.0.0.1`)

### 4.1. `acz_hdtex.dll` Hooks & Helpers
| Symbol / Function | `1.0.2.1` RVA | `2.0.0.1` RVA | Shift | Prologue Bytes (8 B) |
| :--- | :---: | :---: | :---: | :--- |
| `BuildGimArrayGifPackets` | `0x0014CA50` | `0x0014CA90` | `+0x0040` | `48 89 6C 24 18 48 89 74` |
| `InvalidateGsVramRange` | `0x00190400` | `0x00190A80` | `+0x0680` | `40 55 56 41 56 48 8B EC` |
| `BindOrUploadGsVramTexture` | `0x00191040` | `0x001916C0` | `+0x0680` | `40 55 53 56 57 41 54 41` |
| `GsUploadVramBlock` | `0x00191A00` | `0x00192080` | `+0x0680` | `4C 8B DC 53 55 56 41 56` |
| `GsTransferGifPacket` | `0x001924E0` | `0x00192B60` | `+0x0680` | `48 89 5C 24 20 55 56 57` |
| `GameFontRegister` | `0x001FFBD0` | `0x00200250` | `+0x0680` | `48 89 5C 24 08 48 89 74` |
| `GameFontRebuildSlot` | `0x001FFCD0` | `0x00200350` | `+0x0680` | `48 89 5C 24 08 48 89 74` |
| `GimValidateAndInit` | `0x002038C0` | `0x00203F40` | `+0x0680` | `40 53 48 83 EC 20 48 8B` |
| `GimGetHeight` | `0x00203960` | `0x00203FE0` | `+0x0680` | `0F B6 41 08 4C 8B C9 84` |
| `GimGetWidth` | `0x00203A20` | `0x002040A0` | `+0x0680` | `0F B6 41 08 4C 8B C9 84` |
| `GimInitDescriptor` | `0x00203B30` | `0x002041B0` | `+0x0680` | `48 89 5C 24 08 48 89 6C` |
| `ParseFhmGimContainer` | `0x00203D90` | `0x00204410` | `+0x0680` | `48 89 5C 24 10 48 89 74` |
| `LoadTextureFromArcPath` | `0x00214230` | `0x002148B0` | `+0x0680` | `48 89 5C 24 08 48 89 74` |
| `CreateTextureFromPngMemory` | `0x00214420` | `0x00214AA0` | `+0x0680` | `40 55 53 56 57 41 54 41` |
| `GlobalPs2GsVramPtr` (`.data`) | `0x0031CF18` | `0x0031FF18` | `+0x3000` | — |
| `GlobalD3D12Context` (`.data`) | `0x0033AC70` | `0x0033DC90` | `+0x3020` | — |
| `GlobalD3D12CmdList` (`.data`) | `0x0033ACE8` | `0x0033DD08` | `+0x3020` | — |
| `GlobalUploadRingOffset` (`.data`)| `0x003567A8` | `0x003597C8` | `+0x3020` | — |

### 4.2. `memdump.dll` Hooks & Globals
| Symbol / Function | `1.0.2.1` RVA | `2.0.0.1` RVA | Shift |
| :--- | :---: | :---: | :---: |
| `EndFrameAndPresent` | `0x0020C280` | `0x0020C900` | `+0x0680` |
| `IAT_ExitProcess` (`.rdata`) | `0x0027B2E8` | `0x0027D2F8` | `+0x2010` |
| `IAT_PostQuitMessage` (`.rdata`) | `0x0027B6F0` | `0x0027D6E8` | `+0x1FF8` |
| `GlobalD3D12SwapChain` (`.data`) | `0x0033DA48` | `0x00340A68` | `+0x3020` |

### 4.3. `.rdata` Float Constants & Cave (`enhancer.cpp`)
* **`.rdata` Float Constants (`0x2AC000 .. 0x2B9FFF`)**: Shifted by **`+0x2238`** in RVA (`2.0.0.1`). Because instructions in `.text [0x004E80 .. 0x18AD1F]` shifted by **`+0x0040`**, any RIP-relative `disp32` operand in that `.text` region addressing a float constant in `.rdata` increases by **`+0x21F8`** (`0x2238 - 0x0040`).
* **`.rdata` Zero Cave**: `0x00308F00` (`1.0.2.1`) -> `0x0030B500` (`2.0.0.1`), a **`+0x2600`** RVA shift (or **`+0x25C0`** in RIP-relative `disp32` from `.text [+0x0040]`).
