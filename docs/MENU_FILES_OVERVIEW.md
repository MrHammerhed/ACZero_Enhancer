# Ace Combat Zero: The Belkan War — Menu Interface Files Reference (`select/`)

This document provides a complete breakdown of all 37 menu interface files from the `select/` archive (`d.arc`) in the PC port of *Ace Combat Zero: The Belkan War*.

## 1. Architecture and Menu File Format (`.SWA` / `SWAD`)

Files with the `.swa` extension use the proprietary **SWAD** (*Screen Widget Archive Data*) container format created by Namco / Project Aces:

1. **`SWAD` Header**:
   * `+0x00`: Signature `SWAD` (4 bytes).
   * `+0x04`: Total number of embedded chunks (`uint32`).
   * `+0x08`: Offset and byte-size table for each chunk (8 bytes per entry: `uint32 offset`, `uint32 size`).

2. **Chunk #0 — Screen Layout & Animation Bytecode `SWG\0`** (*Screen Widget Graphic*):
   * Vector scene graph, container hierarchy (`_root`, `contents`), buttons, frame animation timelines, input focus rules, and procedural event bindings (`NUD_COMMON_SYS_DECIDE`, `gotoAndPlay`, `menu_endMode`).
   * Every menu screen defines a unique root widget class prefixed with `w` (for example, `wTitle`, `wMainSelect`, `wAircraftSelect`, `wShop`).

3. **Chunks #1..N — Embedded `TIM2` Raster Textures**:
   * Standard PlayStation 2 Graphics Synthesizer (`TIM2`) textures stored in `PSMT4` (4-bit 16-color CLUT), `PSMT8` (8-bit 256-color CLUT with `CSM1` swizzling), `RGB16`, and `RGBA32` formats.
   * These textures are intercepted and replaced by `acz_hdtex.dll` (v0.16+) using triple `FNV-1a` 64-bit hashing (`hash_tim2_norm`, `hash_tim2_pix_clut`, `hash_tim2_pixels`).

## 2. Menu Archive Summary Table (`.swa`)

| File | Size | Chunks | Widget / Engine Mode | TIM2 Count | In-Game Purpose |
| ----- | ----- | ----- | ----- | ----- | ----- |
| **`WakeLogo.swa`** | 1.14 MB | 11 | `wWakeLogo` / `AVCModeWakeLogo` | 10 | Startup splash screens (Namco, Project Aces, Dolby Pro Logic II, CriWare, legal notices). |
| **`Title.swa`** | 688 KB | 9 | `wTitle` / `AVCModeTitle` | 8 | Title screen ("Press Start Button", copyright notices, main game logo). |
| **`MainSelect.swa`** | 2.11 MB | 19 | `wMainSelect` / `AVCModeMainSelect` | 18 | Main Menu (Campaign, Free Mission, VS Mode, The Gauntlet, Records, Options). |
| **`MemCheck.swa`** | 59.7 KB | 4 | `wMemCheck` / `AVCModeWakeMemorycard` | 3 | Save data / memory card check dialogs. |
| **`SaveLoad.swa`** | 1019 KB | 15 | `wSaveLoad` / `AVCSelectMemorycard` | 14 | Manual save and load data screen. |
| **`CampaignSelect.swa`** | 946 KB | 13 | `wCampaignSelect` / `AVCModeCampaignSelect` | 12 | Campaign mission selection and theater-of-operations branching map. |
| **`DetailMap.swa`** | 601 KB | 16 | `wDetailMap` / `AVCSelectASGLine` | 15 | Pre-sortie tactical sector map and operation area overview (area zoom, TGT markers). |
| **`SqData.swa`** | 907 KB | 12 | `wSqData` / `AVCModeBriefing` | 11 | Galm Team squadron data and wingman command configuration (Pixy / PJ). |
| **`AircraftSelect.swa`** | 949 KB | 113 | `wAircraftSelect` / `AVCModeAircraftSelect` | 112 | Main Hangar: aircraft selection, color scheme, special weapon (SPW) loadout, and stats. |
| **`AircraftSelect_Q.swa`** | 1.28 MB | 105 | `wAircraftSelect_` / `option_setQuickSelect` | 104 | Streamlined Quick Select aircraft menu for Free Mission and Versus Mode. |
| **`Shop.swa`** | 924 KB | 117 | `wShop` / `AVCSelectShop` | 116 | Hangar shop for purchasing new fighter aircraft and special weapons. |
| **`AircraftData.swa`** | 357 KB | 15 | `wAircraftData` / `AVCModeAircraftData` | 14 | Aircraft specifications and 3D model viewer with hexagonal radar performance chart. |
| **`AceStyle.swa`** | 960 KB | 13 | `wAceStyle` / `AVCModeAceStyle` | 12 | Ace Style gauge and indicator: Mercenary, Soldier, and Knight. |
| **`CampResult.swa`** | 1.04 MB | 9 | `wCampResult` / `AVCModeCampaignResult` | 8 | Post-mission debriefing results: Rank S/A/B/C/D, targets destroyed, credits earned. |
| **`MissionEnd.swa`** | 903 KB | 15 | `wMissionEnd` / `AVCModeSelector` | 14 | In-flight pause menu and mission end prompts (Retry, Return to Hangar, Abort Mission). |
| **`Unlock.swa`** | 639 KB | 9 | `wUnlock` / `AVCSelectGameSeqData` | 8 | Notification popups for unlocked aircraft, paint schemes, and medals. |
| **`FM_Select.swa`** | 899 KB | 13 | `wFM_Select` / `AVCModeFreeMissionMenu` | 12 | Free Mission mode selection (Mission Select, SP Mission / The Gauntlet). |
| **`FM_Mission.swa`** | 1.48 MB | 42 | `wFM_Mission` / `AVCModeFreeMissionMenu` | 41 | Free Mission stage list with difficulty and route filters. |
| **`VSSet.swa`** | 971 KB | 14 | `wVSSet` / `AVCModeVSMenu` | 13 | Versus Mode match settings (time limit, handicap, victory conditions, split-screen). |
| **`VSstage.swa`** | 1005 KB | 23 | `wVSstage` / `vs_selectStage` | 22 | Versus Mode combat arena selection (B7R Round Table, Directus, Avalon Dam, etc.). |
| **`AssaultRecord.swa`** | 1.86 MB | 211 | `wAssaultRecord` / `AVCModeAssaultRecord` | 210 | Belkan Ace Assault Records: portraits, dossiers, and tactical records for 168 named aces. |
| **`Medals.swa`** | 1.28 MB | 53 | `wMedals` / `AVCModeMedal` | 52 | Service medal showcase for campaign and challenge achievements. |
| **`SceneViewer.swa`** | 1.66 MB | 50 | `wSceneViewer` / `AVCModeSceneViewer` | 49 | Cutscene Theater for replaying unlocked story FMVs and interviews. |
| **`MusicPlayer.swa`** | 889 KB | 15 | `wMusicPlayer` / `AVCModeMusicPlayer` | 14 | Music Player (Jukebox) for listening to the official game soundtrack. |
| **`CM.swa`** | 1.84 MB | 17 | `wCM` / `AVCModeCM` | 16 | Promotional trailers and bonus video viewer. |
| **`ControlSet.swa`** | 1.10 MB | 14 | `wControlSet` / `AVCModePadManual` | 13 | Control settings: axis inversion, button layout, and controller vibration. |
| **`Calibration.swa`** | 903 KB | 16 | `wCalibration` / `AVCSelectCalibration` | 15 | Analog stick and flightstick calibration (deadzones and centering). |
| **`SoundSet.swa`** | 879 KB | 21 | `wSoundSet` / `AVCSelectBGM` | 20 | Audio settings: music, SFX, and voice volume, Stereo / Dolby Pro Logic II modes. |
| **`Brightness.swa`** | 779 KB | 21 | `wBrightness` / `AVCModeSelector` | 20 | Screen brightness and gamma adjustment with calibration gradient bars. |
| **`ScrPosition.swa`** | 828 KB | 12 | `wScrPosition` / `AVCModeSelector` | 11 | Screen position centering and aspect ratio calibration. |
| **`LanguageSet.swa`** | 1.06 MB | 14 | `wControlSet` / `AVCModeSelector` | 13 | Voice and subtitle language selection (JP / US / UK / DE / FR / IT / ES). |

## 3. Auxiliary Configuration & Resource Files

| File | Size | Format | Purpose and Internal Structure |
| ----- | ----- | ----- | ----- |
| **`aircraft_datas.bin`** | 864 B | 36 records × 24 B | Binary database of stats for all 36 aircraft (Speed, Mobility, Stability, Defense, Air-to-Air, Air-to-Ground, buy price, sell price). |
| **`aircraft_camera.bin`** | 320 B | 80 float values | Coordinates and angles for the 5 Hangar 3D camera views (front quarter, profile, top-down, cockpit, exhaust nozzles) separated by `FLT_MAX`. |
| **`weapon_datas.bin`** | 160 B | 20 records × 8 B | Parameters and compatibility flags for all 20 special weapons (SPW: UGBL, QAAM, SAAM, XLAA, XMAA, SFFS, TLS, MPBM, etc.). |
| **`loading.bin`** | 77.3 KB | 6 GIM textures | Textures for the animated "NOW LOADING..." indicator, circular radar scanner, and crosshair. |
| **`selectse.hsh`** | 90.8 KB | `SHT\0` bank | Menu sound effect index (cursor movement, confirm, cancel, shop purchase, Ace Style chime). |
| **`spiderUnti.gim`** | 624 B | GIM texture (16×16) | Vertex dot marker for the 6-axis hexagonal aircraft performance radar chart in `AircraftData`. |

## 4. Logical Screen Flow

### 4.1. Startup and Main Menu
* `WakeLogo.swa` $\rightarrow$ `Title.swa` $\rightarrow$ `MemCheck.swa` $\rightarrow$ `MainSelect.swa` $\rightarrow$ `SaveLoad.swa`.

### 4.2. Briefing and Sortie Preparation (Campaign)
* `CampaignSelect.swa`: mission and operation branch (A/B/C) selection.
* `DetailMap.swa`: tactical map inspection and target (TGT) review.
* `SqData.swa`: wingman doctrine selection.
* `AircraftSelect.swa`: aircraft and special weapon selection.
* `Shop.swa`: purchasing new aircraft and special weapons.
* `AircraftData.swa` + `spiderUnti.gim` + `aircraft_datas.bin` + `aircraft_camera.bin`: detailed performance comparison.

### 4.3. Records and Belkan Ace Anthology (Records)
* `AssaultRecord.swa`: Belkan ace registry with personal emblems, historical notes, and aircraft portraits.
* `AceStyle.swa`: dynamic Ace Style compass.
* `Medals.swa`: collection of all 32 service medals.
* `SceneViewer.swa` & `MusicPlayer.swa`: cutscene theater and soundtrack player.

### 4.4. System Options (Options)
* `ControlSet.swa`, `Calibration.swa`, `SoundSet.swa`, `Brightness.swa`, `ScrPosition.swa`, `LanguageSet.swa`.
