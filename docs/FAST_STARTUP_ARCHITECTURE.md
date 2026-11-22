# Multi-Stage Fast Startup Architecture (`FastStartup = 0..3`)

This document describes the startup flow patches, hooks, and dual-version (`v1.0.2.1` / `v2.0.0.1`) RVAs implemented by the **`FastStartup`** engine in **`enhancer.asi`** (`v0.24`).

---

## 1. Configuration (`mods\enhancer\enhancer.ini`)

Controlled by `FastStartup` under `[Enhancer]` (clamped to `0..3`, default `0`):

| Level | Mode | Behavior |
| :---: | :--- | :--- |
| **`0`** | **Disabled** *(Default)* | Standard vanilla startup sequence (launcher, pad manual, Memory Card UI delays, unskippable intro logos, Title Screen). |
| **`1`** | **Quick Boot + Press-to-Skip Logos** | Bypasses the native launcher (`e.arc` mount & dialog) and controller manual screen (`CModePadManual`), and hooks `CModeWakeLogo::Update` so pressing any gamepad button or `Enter` / `Space` / `Esc` skips intro logos cleanly to the Title Screen (`CModeTitle`). |
| **`2`** | **Fast Memory Card + Auto-Skip to Title** | Includes Level `1` + removes the `2x30`-frame (~`1.0s`) artificial Memory Card UI timers in `CSelectMemorycard` and automatically redirects post-save boot directly to the Title Screen (`CModeTitle`). |
| **`3`** | **Direct to Main Menu** | Includes Level `2` + automatically skips both the intro logos and the Title Screen directly into the Main Menu (`CModeMainSelect`). |

---

## 2. Technical Breakdown by Stage

### Stage 1 (`FastStartup >= 1`): Launcher & Pad Manual Bypass
1. **Native Launcher Bypass**:
   - Sets the in-memory `SkipLauncher` flag (`0x32C939` in `v1.0.2.1` / `0x32F958` in `v2.0.0.1`) to `1` and patches the `sete` write (`0x223151` / `0x224351`) to `mov byte ptr [rip+...], 1`.
   - Converts the conditional `jne` branches guarding `e.arc` mounting (`0x221BDA` / `0x222D3F`) and the launcher dialog (`0x221F31` / `0x2230F6`) into unconditional `jmp` instructions.
2. **Controller Manual Screen (`CModePadManual`) Bypass**:
   - In `AVInitMode::Update` (`AczRva(0x16348B)` -> `0x16348B` in `v1.0.2.1` / `0x1634CB` in `v2.0.0.1`), redirects the `lea rax, [rip+...]` next-mode factory pointer from `CModePadManual` (`0x178190` / `0x1781D0`) directly to `CModeWakeMemorycard` (`0x1781D0` / `0x178210`).
3. **Press-to-Skip Intro Logos (`FastStartup == 1` only)**:
   - Installs a 64-bit detour on `CModeWakeLogo::Update` (`AczRva(0x17DC30)` -> `0x17DC30` in `v1.0.2.1` / `0x17DC70` in `v2.0.0.1`).
   - Uses rising-edge detection (`cur & ~prev`) across live merged gamepad buttons (`g_LiveInputButtons` / `ControllerState[0] + 0x28`) and foreground keyboard keys (`VK_RETURN`, `VK_SPACE`, `VK_ESCAPE`) while `this->state == 1` (`[rcx + 0x90]`).
   - On button press, sets `this->state = 2` so `CModeWakeLogo` finishes cleanly and transitions to `CModeTitle`, while arming `g_SuppressInputFramesAfterLogoSkip = 6` to prevent the same press from accidentally advancing past the Title Screen.

### Stage 2 (`FastStartup >= 2`): Memory Card UI Timer Removal & Auto-Skip to Title
1. **Memory Card Delay Removal (`CSelectMemorycard`)**:
   - Patches `mov dword ptr [rcx+18h], 1Eh` in `CSelectMemorycard::modeWake` (`AczRva(0x1C3000)`) and `mov dword ptr [rcx+1Ch], 1Eh` in `CSelectMemorycard::modeLoad` (`AczRva(0x1C2B27)`) from `30` frames (`0x1E`) to `0` frames, removing ~1 second of artificial wait while keeping save loading intact.
2. **Post-Save Redirect (`FastStartup == 2`)**:
   - In `CModeWakeMemorycard::Init` (`AczRva(0x17D513)`), redirects the `lea rcx, [rip+...]` next-mode factory from `CModeWakeLogo` directly to `CModeTitle` (`0x1782B0` / `0x1782F0`).

### Stage 3 (`FastStartup >= 3`): Direct to Main Menu
- In `CModeWakeMemorycard::Init` (`AczRva(0x17D513)`), redirects the next-mode factory directly to `CModeMainSelect` (`0x178330` / `0x178370`), bypassing both intro logos and the Title Screen.

---

## 3. Dual-Version RVA Summary (`1.0.2.1` vs `2.0.0.1`)

| Target | `1.0.2.1` RVA | `2.0.0.1` RVA | Shift | Purpose |
| :--- | :---: | :---: | :---: | :--- |
| `SkipLauncherFlag` (`.data`) | `0x0032C939` | `0x0032F958` | `+0x301F` | Force launcher skip flag |
| `LauncherSetFlag` (`sete`) | `0x00223151` | `0x00224351` | `+0x1200` | Force `SkipLauncher = 1` |
| `LauncherMountEArc` (`jne`) | `0x00221BDA` | `0x00222D3F` | `+0x1165` | Skip `e.arc` mount |
| `LauncherDialogBranch` (`jne`) | `0x00221F31` | `0x002230F6` | `+0x11C5` | Skip launcher dialog |
| `AVInitMode::Update` (`lea rax`) | `0x0016348B` | `0x001634CB` | `+0x0040` | Redirect `CModePadManual` -> `CModeWakeMemorycard` |
| `CSelectMemorycard::modeLoad` | `0x001C2B27` | `0x001C31A7` | `+0x0680` | Zero 30-frame load UI timer |
| `CSelectMemorycard::modeWake` | `0x001C3000` | `0x001C3680` | `+0x0680` | Zero 30-frame wake UI timer |
| `CModeWakeLogo::Update` | `0x0017DC30` | `0x0017DC70` | `+0x0040` | Press-to-skip intro logos (`FastStartup=1`) |
| `CModeWakeMemorycard::Init` (`lea rcx`) | `0x0017D513` | `0x0017D553` | `+0x0040` | Redirect post-save boot -> `CModeTitle` (`2`) or `CModeMainSelect` (`3`) |

---

## 4. Operational Notes & Diagnostics

- **Save Data Safety**: All levels (`1..3`) preserve the full `CModeWakeMemorycard` save-load sequence; only artificial UI timers and splash/title screens are skipped.
- **Diagnostics**: The active `FastStartup` level is logged at startup in `mods\enhancer\enhancer.log` and reported in `memdump.asi` diagnostic reports via `AczEnhancer_WriteStateSummary`.
- **Foreground Check**: Keyboard skip keys (`Enter`, `Space`, `Esc`) in `FastStartup=1` are only accepted when the `acz.exe` window is in the foreground.
