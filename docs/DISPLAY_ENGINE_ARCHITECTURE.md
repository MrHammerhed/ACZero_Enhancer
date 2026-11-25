# ACZero Enhancer — Live Display Engine, 1:1 UI Scaling & Enhancer Settings

Документ фиксирует всё, что реализовано в рамках подсистемы живого управления разрешением, режимами экрана (`Windowed` / `Borderless`), автоматическим соотношением сторон и обновлённым интерфейсом `ENHANCER SETTINGS`, а также ключевые технические нюансы и ограничения.

---

## 1. Архитектура движка разрешений ([acz_display_engine.h](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/acz_display_engine.h))

### 1.1. Снятие ограничений на старте (`v1.0.2.1` и `v2.0.0.1`)
- **AOB-патч `LoadSettingIni`**: обходит жёсткий whitelist высот (`2160/1800/1440/...`) и проверку строгого `16:9 / 4:3`, благодаря чему игра на старте принимает любые кастомные разрешения (`1920×822`, `1826×1026`, `2560×1080`, `3440×1440`, `5120×1440`, `3:2` и др.), а также принудительно выставляет `g_Is4x3Window = 0`.
- **Отключение стоковой обрезки `< 1.6778f`**: в `CreateWindowSizeDependentResources` патч (`41 0F 92 C0` → `45 31 C0 90`) отключает зум/кроп вьюпорта, обеспечивая честный вывод 1:1 при любых соотношениях сторон.
- **Независимость от версии `acz.exe`**: все внутренние адреса (`CreateWindowSizeDependentResources`, `WaitForGpu`, `g_DeviceResourcesPtr`, `g_D3D12CtxPtr`, `g_RenderWidth/Height`, `g_ClientW/H`, `g_GsInternalW/H`, `g_RtHashMap`, `g_FeedbackRtCache`, `CreateOffscreenRT`, `CreateAllGsRTs`, `ClearRtMap` и др.) резолвятся динамически из функций ниже `0x21E068` — безопасно для `v1.0.2.1` и `v2.0.0.1`.

### 1.2. Управление окном Win32 и `Alt+Enter`
- Сабклассинг игрового окна (`GWLP_WNDPROC`) снимает блокировку `WM_GETMINMAXINFO` и перехватывает `Alt+Enter`, `SC_MAXIMIZE` и `SC_RESTORE` для живого переключения `Windowed ↔ Borderless`.
- **Borderless** всегда использует нативное разрешение текущего монитора (multi-monitor aware через `MonitorFromWindow` + `GetMonitorInfoW`).
- **Windowed** выставляет точный размер клиентской области (`AdjustWindowRectEx`) и центрирует окно в рабочей области монитора.
- Во время программного ресайза выставляется флаг `g_SuppressWndProcResize`, чтобы стоковый `WM_SIZE` не конфликтовал с нашей последовательностью ресайза.

### 1.3. Безопасная живая пересборка SwapChain и 26 GS RenderTargets (без перезапуска и утечек VRAM)
Пересборка выполняется строго в два безопасных межфазных момента:
1. **`OnAfterPresent()`** (вызывается в конце `Hook_EndFrameAndPresentUi`, когда command list уже закрыт):
   - Выполняет `WaitForGpu` (полная синхронизация GPU).
   - Сбрасывает кэшированные указатели backbuffer RTV в [acz_ui_renderer.h](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/ui/acz_ui_renderer.h) (`InvalidateBackbuffers()`), чтобы DXGI `ResizeBuffers` не столкнулся с лишними ссылками.
   - Меняет стиль и размер Win32-окна, обновляет `RenderWidth/Height`, `ClientW/H`, `desiredOutputRect` и выставляет `m_needsResize (+0xE4) = 1`.
   - Ставит в очередь пересборку GS RT (`g_NeedRtRebuild = true`).
2. **`Hook_BeginFrame()` → `OnAfterBeginFrame()`** (хук на `BeginFrame` `+0x20B9C0`, 16-байтный пролог без RIP-инструкций):
   - Сначала отрабатывает оригинальный `BeginFrame()` — он вызывает `CreateWindowSizeDependentResources` (`ResizeBuffers`), открывает `m_commandList` и кладёт `-1` (SwapChain backbuffer) в `g_RtStack`.
   - Сразу после этого `OnAfterBeginFrame()` обнуляет кэши `g_FeedbackRtCache` и `[g_GsMainPtr + 0xD0]`, вызывает `ClearRtMap` (освобождает все старые D3D12-ресурсы без утечек VRAM), записывает новые `g_GsInternalW/H`, пересоздаёт RT #0 (`1920×1080` для FMV/брифингов — гарантированно получает слот `0`) и вызывает `CreateAllGsRTs` для пересоздания всех 26 GS RT.

> [!IMPORTANT]
> **Защита от 16-битного переполнения `XYZ2` в PS2 GS**: внутренние размеры GS RenderTargets пропорционально ограничиваются до **`≤ 3840×3838`** (ниже порога `3967×3839`). Например, на `5120×1440` (`32:9`) SwapChain и UI рендерятся в полном `5120×1440`, а внутренние GS RT — в `3840×1080` с сохранением точных пропорций.

### 1.4. Список разрешений и `settings/setting.ini`
- В `RefreshResolutionList()` действуют правила:
  - Жёсткий минимум **`≥ 1280×720`** (все более мелкие режимы драйвера отфильтровываются).
  - Подхватываются все реальные 32-битные режимы текущего монитора через `EnumDisplaySettingsW` (включая `3:2`, `16:10`, `21:9`, `32:9` и кастомные разрешения).
  - Всегда включается нативное разрешение монитора и текущее активное оконное разрешение.
  - Добавляются стандартные пресеты `16:9 / 16:10`, `4:3` (`1280×960`, `1440×1080`, `1920×1440`, `2880×2160`) и `21:9` (`1680×720`, `1920×822`, `2560×1080`, `3440×1440`, `3840×1600`, `5120×2160`) в пределах габаритов монитора, плюс динамические пары `4:3` и `21:9` от каждого режима (например, для `1826×1026` → `1368×1026 (4:3)` и `1826×782 (21:9)`).
  - Поддерживаемые теги в списке: `(4:3)`, `(5:4)`, `(16:10)`, `(16:9)`, `(21:9)`, `(32:9)`.
- **Сохранение `settings/setting.ini`**: обновляются только `Resolution` и `ScreenMode`, а все комментарии и остальные ключи (`Msaa`, `SystemLanguage`, `SkipLauncher`, `Player1`, `Player2`) сохраняются нетронутыми.

---

## 2. Непрерывная математика соотношения сторон ([enhancer.cpp](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/enhancer.cpp))

- Ручной выбор `ASPECT RATIO` убран — `GetEffectiveAspectRatio()` всегда автоматически вычисляет реальное соотношение сторон от активного выходного разрешения (`GetPhysicalOutputAspect()`).
- Вся графика и игровые элементы переведены с дискретных проверок `aspect_opt == 2` на непрерывный расчёт:
  - 3D-проекции (`Hook_GsBuildProjectionMatrices`), динамический pillarbox (нативный 4:3 при `≤ 1.38`, плавный для любых `> 1.38`), дебрифинг, FMV-ролики (`Hook_DrawMovieQuad`), катсцены, экран загрузки, радар, HUD и меню паузы.
- В экспорт диагностики `AczEnhancer_WriteStateSummary` (используется `memdump.asi`) добавлена телеметрия активного режима (`Mode`, `Output`, `InternalGS`, `EffectiveAspect`).

---

## 3. 1:1 масштабирование RmlUi и обновлённый интерфейс ([acz_rmlui_backend.h](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/ui/acz_rmlui_backend.h))

### 3.1. Идентичная вёрстка 1в1 на любом разрешении
- **Проблема, которую решили**: в `acz_theme.rcss` все размеры и шрифты заданы в `px`, а `SetDensityIndependentPixelRatio()` в RmlUi масштабирует только `dp`. Из-за этого на `1280×720` интерфейс выглядел в 1.5 раза крупнее, чем на `1920×1080`, и строки переносились.
- **Решение**:
  - RmlUi-контекст всегда работает в едином виртуальном пространстве на базе `1080p`:
    - `uiScale = min(w / 1920.0f, h / 1080.0f)`
    - `virtW = round(w / uiScale)`, `virtH = round(h / uiScale)`
    - `m_Context->SetDimensions({virtW, virtH})`, `SetDensityIndependentPixelRatio(1.0f)`
  - `AczRmlRenderInterface` равномерно масштабирует на `m_UiScale` все вершины в `RenderGeometry` и прямоугольники отсечения в `EnableScissorRegion` / `SetScissorRegion`.
  - **Итог**: на `720p`, `900p`, `1080p`, `1440p`, `4K` и `21:9` вёрстка, шрифты, отступы и переносы строк полностью идентичны **1в1** без правок `acz_theme.rcss`.

### 3.2. `ENHANCER SETTINGS > GRAPHICS` и единый 10-секундный диалог
- Состав категории `GRAPHICS`:
  1. `DISPLAY MODE` (`WINDOWED` / `BORDERLESS`, отложенное применение)
  2. `RESOLUTION` (прокручиваемый список до 8 видимых строк с правым скроллбаром; в `BORDERLESS` отображает нативное разрешение монитора и заблокирован от ручной смены)
  3. `WIDESCREEN OVERRIDE` (`AUTO` / `FORCE 4:3` / `FORCE 16:9`, мгновенное применение, сохраняется в `enhancer.ini` → `[UI] WidescreenOverride`)
  4. `EXTENDED DRAW DISTANCE`
  5. `SPHERICAL RADIAL FOG`
- **Единое подтверждение при выходе**: отдельный пункт `APPLY DISPLAY` убран. При нажатии **Назад (`CANCEL` / `←`)** из `GRAPHICS` с несохранёнными изменениями режима или разрешения:
  - Изменения **сразу применяются** в реальном времени.
  - Открывается один диалог в аутентичном стиле оригинального ACZero (`Cancel load?`): полупрозрачный тёмно-бордовый прямоугольник с тонкой золотой рамкой, вопрос `Keep display settings? (10)` в левом верхнем углу и вертикальные кнопки **`YES`** / **`NO`** по центру (по умолчанию выбрано **`NO`** для безопасности).
  - **`YES`**: сохраняет настройки в `settings/setting.ini` и выходит в `ENHANCER SETTINGS`.
  - **`NO` / `CANCEL` / истечение 10 секунд**: откатывает режим и разрешение назад и оставляет игрока в `GRAPHICS`.

### 3.3. `WIDESCREEN OVERRIDE` для ультрашироких экранов
- В `ApplyAspectLayout()`:
  - **`AUTO (0)`**: до `16:9` стандартные отступы CSS `4.8%`; на `> 16:9` рамка плавно расширяется до `2160px` и центрируется.
  - **`FORCE 4:3 (1)`**: ограничивает и центрирует ширину папки меню, верхних подсказок и футера под пропорции `4:3` (`1440px` на `1080p`).
  - **`FORCE 16:9 (2)`**: ограничивает и центрирует ширину меню на `21:9` / `32:9` под пропорции `16:9` (`1920px` на `1080p`).
  - Анимированный фон (градиент, командный центр, сетка, scanline и искры) при этом всегда остаётся на весь экран.

### 3.4. Оформление `ENHANCER SETTINGS` в стиле стартового `MAIN MENU`
- В `ENHANCER SETTINGS` (Depth 2) используются те же пропорции колонок, что и на стартовом экране (`41.5% / 29% / 29.5%`) — длинные названия во 2-й колонке больше не переносятся и идеально выровнены с 3-й колонкой.
- Включена правая полоса прокрутки и синхронный плавный скролл 2-й и 3-й колонок (`#col-mid-scroll` и `#col-right-scroll`) при переходе к `HD TEXTURES` и `DIAGNOSTICS` (а также плавный скролл 3-й колонки при наведении на `ENHANCER SETTINGS` в `OPTIONS`).

---

## 4. Известные нюансы и ограничения

- **MSAA намеренно не вынесен в UI**: сохраняется значение `Msaa` из `settings/setting.ini` без изменений (планируется замена на FSR/DLSS в будущем).
- **Тег `(3:2)` в списке разрешений**: разрешения `3:2` автоматически подхватываются через `EnumDisplaySettingsW` и работают корректно, но отображаются без текстового тега `(3:2)` в скобках (при желании добавляется одной строкой в `FormatAspectTag`).
- **Стили диалога подтверждения**: `#display-confirm-overlay` в [main_menu.rml](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/mods/enhancer/ui/main_menu.rml) стилизован полностью через inline-стили в C++, чтобы не затрагивать кастомный `acz_theme.rcss` в папке игры.
- **Совместимость**: подтверждена чистая работа в связке с `acz_hdtex.asi`, `acz_radial_fog`, `acz_draw_distance` и `memdump.asi`.

---

## 5. Изменённые и созданные файлы

- [acz_display_engine.h](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/acz_display_engine.h) — движок live-разрешений, режимов окна, пересборки SwapChain/GS RT и `setting.ini`
- [enhancer.cpp](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/enhancer.cpp) — интеграция хуков, непрерывный аспект, `WidescreenOverride` INI и телеметрия
- [acz_ui_renderer.h](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/ui/acz_ui_renderer.h) — `InvalidateBackbuffers()` перед `ResizeBuffers`
- [screen_options.h](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/ui/screen_options.h) — `OptionsHostBridge` для Display Engine и `Widescreen Override`
- [acz_rmlui_backend.h](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/ui/acz_rmlui_backend.h) — 1:1 масштабирование RmlUi, прокрутка списков, единый 10-сек диалог ACZero, `Widescreen Override` и стиль `ENHANCER SETTINGS`
- [main_menu.rml](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/mods/enhancer/ui/main_menu.rml) — контейнер `#display-confirm-overlay`
- [strings_en.ini](file:///e:/Games/Ace%20Combat%20Zero%20The%20Belkan%20War/ACZero%20Enhancer/mods/enhancer/ui/strings_en.ini) — локализация новых пунктов и диалога подтверждения
