# ACZero Enhancer — Custom Localization Architecture Roadmap

## 1. Core Concept
To avoid conflicts when installing community translations and language packs alongside widescreen UI layout overrides (`.swa` / `16:9` / `21:9`), the text subsystem is divided into three layers:

1. **Layout & ActionScript Logic Layer (`.swa` / `SWG`)**:
   - Raw language-specific strings are never hardcoded inside menu bytecode or `TextField` (`DictType 2` / `DictType 3`) records.
   - All text fields reference string keys from the `SHT` hash table (such as `H11111_002`, `H00000_029`, `H11111_005`, `IM000_002`) via script variables (`@57..@59`, `@76..@78`) or direct string identifiers.

2. **Base Resource Layer (`.msa` Containers)**:
   - Default English strings are placed in reserved (`DUMMY`) slots inside `select.msa` (and other `.msa` containers), and missing glyphs (such as the digit `'2'` in `Pool 7`) are appended to the font pools.
   - In addition, `enhancer.dll` / `acz_hdtex.dll` patches these slots and the `'2'` glyph in RAM during font initialization (`Hook_GameFontInit` / `Hook_FallbackGameFontInit`) when `select.msa` is loaded directly from `d.arc`.

3. **External Localization Override Layer (`mods/locale/strings_<lang>.ini`) — Future Stage**:
   - A hook on the string lookup function checks for UTF-8 overrides in an external `.ini` or `.tsv` file located in `mods/locale/`.
   - Translators will not need to repack binary `.msa` or `.swa` archives — editing a plain-text file will be sufficient:
     ```ini
     [MainSelect]
     H11111_002 = 21:9
     H00000_029 =  21:9
     H11111_005 = >21:9
     IM000_002 = Optimal for 21:9 ultrawide monitors.
     ```
   - To support extended alphabets (e.g., Cyrillic or other non-Latin scripts), the font pool generator will be able to load extended glyph tables and HD font atlases matched by font pool hash.
