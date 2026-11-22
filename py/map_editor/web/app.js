import { Viewport3D } from './viewport3d.js';

const WORLD_MIN = -262144.0;
const WORLD_SIZE = 524288.0;
const TILE_SIZE = 2048.0;
const EFA_DIM = 128;

const state = {
  maps: [],
  mapName: null,
  meta: null,
  objects: [],
  models: [],
  efa: new Uint8Array(EFA_DIM * EFA_DIM).fill(255),
  heightGrid: null,
  heightDim: 0,
  atlasInfo: { cols: 1, tileCount: 1, tilePx: 128 },

  selectedIndex: -1,
  activeTool: 'select',
  activeModelId: 0,
  shading: 'hd', // 'wire' | 'vanilla' | 'hd'
  autoSnapY: true,
  brushRadius: 2,
  brushErase: false,
  layers: { terrain: true, objects: true, forest: false, sectors: true, wireframe: false },

  dirty: false,
  undoStack: [],
  redoStack: [],
};

const $ = (id) => document.getElementById(id);
const mapSelect = $('map-select');
const overrideBadge = $('override-badge');
const modeSelect = $('mode-select');
const objCountEl = $('obj-count');
const mdlCountEl = $('mdl-count');
const tabObjectsEl = $('tab-objects');
const tabModelsEl = $('tab-models');
const listSearchEl = $('list-search');
const noSelEl = $('no-selection');
const objFieldsEl = $('obj-fields');
const inspIdEl = $('insp-id');
const inspModelEl = $('insp-model');
const inspXEl = $('insp-x');
const inspYEl = $('insp-y');
const inspZEl = $('insp-z');
const inspTileEl = $('insp-tile');
const placeModelEl = $('place-model-select');
const efaCountEl = $('efa-count');
const statusMsgEl = $('status-msg');
const statusCoordsEl = $('status-coords');
const viewLabelEl = $('view-label');
const viewSublabelEl = $('view-sublabel');
const ctxMenuEl = $('context-menu');

function toast(msg, kind = 'info') {
  const host = $('toast-container');
  const el = document.createElement('div');
  el.className = `toast ${kind}`;
  el.textContent = msg;
  host.appendChild(el);
  setTimeout(() => el.remove(), 3500);
}

function setStatus(msg) {
  statusMsgEl.textContent = msg;
}

function worldToTile(x, z) {
  const gx = Math.max(0, Math.min(255, Math.floor((x - WORLD_MIN) / TILE_SIZE)));
  const gz = Math.max(0, Math.min(255, Math.floor((z - WORLD_MIN) / TILE_SIZE)));
  return [gx, gz];
}

function sampleTerrainY(x, z) {
  if (!state.heightGrid || state.heightDim < 2) return 0.0;
  const dim = state.heightDim;
  const gx = Math.max(0, Math.min(dim - 1, ((x - WORLD_MIN) / WORLD_SIZE) * (dim - 1)));
  const gz = Math.max(0, Math.min(dim - 1, ((z - WORLD_MIN) / WORLD_SIZE) * (dim - 1)));
  const ix = Math.floor(gx);
  const iz = Math.floor(gz);
  const ix1 = Math.min(dim - 1, ix + 1);
  const iz1 = Math.min(dim - 1, iz + 1);
  const tx = gx - ix;
  const tz = gz - iz;
  const h00 = state.heightGrid[iz * dim + ix];
  const h10 = state.heightGrid[iz * dim + ix1];
  const h01 = state.heightGrid[iz1 * dim + ix];
  const h11 = state.heightGrid[iz1 * dim + ix1];
  return (
    h00 * (1 - tx) * (1 - tz) +
    h10 * tx * (1 - tz) +
    h01 * (1 - tx) * tz +
    h11 * tx * tz
  );
}

function makeSnapshot() {
  return {
    objects: state.objects.map((o) => ({ ...o })),
    efa: new Uint8Array(state.efa),
    selectedIndex: state.selectedIndex,
  };
}

function pushUndo() {
  state.undoStack.push(makeSnapshot());
  if (state.undoStack.length > 50) state.undoStack.shift();
  state.redoStack = [];
  state.dirty = true;
  updateUndoButtons();
}

function restoreSnapshot(snap) {
  state.objects = snap.objects.map((o) => ({ ...o }));
  state.efa = new Uint8Array(snap.efa);
  state.selectedIndex = Math.min(snap.selectedIndex, state.objects.length - 1);
  refreshAllViews();
  updateUndoButtons();
}

function updateUndoButtons() {
  $('btn-undo').disabled = state.undoStack.length === 0;
  $('btn-redo').disabled = state.redoStack.length === 0;
  overrideBadge.classList.toggle('hidden', !(state.dirty || state.meta?.has_overrides));
}

function uint8ToBase64(bytes) {
  let bin = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    bin += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(bin);
}

// ============================================================================
// 3D Viewport & Blender Context Menu
// ============================================================================

const vp3d = new Viewport3D($('canvas-3d-host'), $('nav-gizmo'), {
  onSelectObject: (idx) => selectObject(idx),
  onGizmoDragStart: () => pushUndo(),
  onGizmoDragEnd: () => refreshObjectList(),
  onObjectDragged: (idx, wx, wy, wz) => {
    const o = state.objects[idx];
    if (!o) return;
    o.x = Math.max(WORLD_MIN, Math.min(-WORLD_MIN, wx));
    o.z = Math.max(WORLD_MIN, Math.min(-WORLD_MIN, wz));
    o.y = state.autoSnapY ? sampleTerrainY(o.x, o.z) : wy;
    if (state.autoSnapY) vp3d.updateObjectTransform(idx, o.x, o.y, o.z);
    updateInspector();
  },
  onPlaceObject: (wx, wy, wz) => placeObjectAt(wx, wy, wz),
  onPaintForest: (wx, wz, isStart) => paintForestAt(wx, wz, isStart),
  onForestStrokeEnd: () => { lastPaintWorld = null; updateForestStats(); },
  onHoverWorld: (wx, wy, wz) => updateHoverCoords(wx, wy, wz),
  onViewChanged: ({ isOrtho, preset, flyMode }) => {
    const projLabel = isOrtho ? 'Orthographic' : 'Perspective';
    viewLabelEl.textContent = flyMode ? 'Walk / Fly Mode (LMB / Esc to exit)' : `${preset} ${projLabel}`;
    const btn = $('btn-nav-proj');
    btn.textContent = isOrtho ? 'Ortho' : 'Persp';
    btn.classList.toggle('ortho-on', isOrtho);
  },
  onContextMenu: (clientX, clientY, hitObjIdx, worldPt) => {
    if (hitObjIdx >= 0 && hitObjIdx !== state.selectedIndex) {
      selectObject(hitObjIdx);
    }
    openContextMenu(clientX, clientY, worldPt);
  },
});

function closeContextMenu() {
  ctxMenuEl.classList.add('hidden');
}

function openContextMenu(x, y, worldPt) {
  const hasSel = state.selectedIndex >= 0 && state.objects[state.selectedIndex];
  const items = [];

  if (hasSel) {
    const o = state.objects[state.selectedIndex];
    items.push({ header: `Object #${state.selectedIndex} (Model #${o.model_id})` });
    items.push({ label: 'Move / Axis Lock', kbd: 'G', fn: () => { setTool('select'); vp3d.clearAxisLock(); } });
    items.push({ label: 'Snap Y to Terrain', fn: () => snapSelectedY() });
    items.push({ label: 'Duplicate Object', kbd: 'Shift+D', fn: () => duplicateSelected() });
    items.push({ label: 'Frame Selected', kbd: 'Num .', fn: () => focusSelected() });
    items.push({ label: 'Delete Object', kbd: 'Del', danger: true, fn: () => deleteSelected() });
    items.push({ sep: true });
  } else {
    items.push({ header: 'Viewport Context Menu' });
  }

  if (worldPt) {
    items.push({
      label: `Place Prefab #${state.activeModelId} Here`,
      fn: () => placeObjectAt(worldPt.x, worldPt.y, worldPt.z),
    });
    items.push({
      label: 'Focus Camera Here',
      fn: () => vp3d.focusWorldPoint(worldPt.x, worldPt.y, worldPt.z, 120),
    });
    items.push({ sep: true });
  }

  items.push({ label: 'Top Orthographic View', kbd: 'Num 7', fn: () => vp3d.setViewPreset('top', true) });
  items.push({ label: 'Toggle Persp / Ortho', kbd: 'Num 5', fn: () => vp3d.toggleProjection() });
  items.push({ label: 'Frame Whole Map', kbd: 'Home', fn: () => vp3d.frameAll() });
  items.push({ sep: true });
  items.push({ label: 'CV Detect / PBR Tools (Planned)', disabled: true });

  ctxMenuEl.innerHTML = '';
  for (const it of items) {
    if (it.sep) {
      const s = document.createElement('div');
      s.className = 'bl-sep';
      ctxMenuEl.appendChild(s);
    } else if (it.header) {
      const h = document.createElement('div');
      h.className = 'bl-ctx-header';
      h.textContent = it.header;
      ctxMenuEl.appendChild(h);
    } else {
      const b = document.createElement('button');
      if (it.danger) b.className = 'danger-item';
      if (it.disabled) b.disabled = true;
      b.innerHTML = `<span>${it.label}</span>${it.kbd ? `<span class="kbd">${it.kbd}</span>` : ''}`;
      if (!it.disabled && it.fn) {
        b.addEventListener('click', () => {
          closeContextMenu();
          it.fn();
        });
      }
      ctxMenuEl.appendChild(b);
    }
  }

  ctxMenuEl.classList.remove('hidden');
  const rect = ctxMenuEl.getBoundingClientRect();
  const clampedX = Math.min(x, window.innerWidth - rect.width - 8);
  const clampedY = Math.min(y, window.innerHeight - rect.height - 8);
  ctxMenuEl.style.left = `${Math.max(4, clampedX)}px`;
  ctxMenuEl.style.top = `${Math.max(4, clampedY)}px`;
}

// ============================================================================
// Actions
// ============================================================================

function updateHoverCoords(wx, wy, wz) {
  const y = wy !== null && wy !== undefined ? wy : sampleTerrainY(wx, wz);
  const [gx, gz] = worldToTile(wx, wz);
  statusCoordsEl.textContent = `X: ${wx.toFixed(1)}  Y: ${y.toFixed(1)}  Z: ${wz.toFixed(1)}  | Tile: (${gx}, ${gz})`;
}

function selectObject(idx) {
  state.selectedIndex = idx;
  vp3d.setSelected(idx);
  updateInspector();
  highlightListSelection();
  if (idx >= 0 && state.objects[idx]) {
    const o = state.objects[idx];
    viewSublabelEl.textContent = `Active: Object #${idx} · Model #${o.model_id}`;
  } else {
    viewSublabelEl.textContent = 'No Active Object';
  }
}

function placeObjectAt(wx, wy, wz) {
  pushUndo();
  const y = state.autoSnapY || wy === null ? sampleTerrainY(wx, wz) : wy;
  state.objects.push({
    id: state.objects.length,
    x: wx,
    y: y,
    z: wz,
    model_id: state.activeModelId,
  });
  state.selectedIndex = state.objects.length - 1;
  refreshAllViews();
  setStatus(`Placed Model #${state.activeModelId} at (${wx.toFixed(0)}, ${y.toFixed(1)}, ${wz.toFixed(0)})`);
}

function snapSelectedY() {
  const o = state.objects[state.selectedIndex];
  if (!o) return;
  pushUndo();
  o.y = sampleTerrainY(o.x, o.z);
  vp3d.updateObjectTransform(state.selectedIndex, o.x, o.y, o.z);
  updateInspector();
}

function focusSelected() {
  const o = state.objects[state.selectedIndex];
  if (o) vp3d.focusWorldPoint(o.x, o.y, o.z, 90);
}

function duplicateSelected() {
  const o = state.objects[state.selectedIndex];
  if (!o) return;
  pushUndo();
  const nx = o.x + 250;
  const nz = o.z + 250;
  const ny = state.autoSnapY ? sampleTerrainY(nx, nz) : o.y;
  state.objects.push({ id: state.objects.length, x: nx, y: ny, z: nz, model_id: o.model_id });
  state.selectedIndex = state.objects.length - 1;
  refreshAllViews();
}

function deleteSelected() {
  if (state.selectedIndex < 0) return;
  pushUndo();
  state.objects.splice(state.selectedIndex, 1);
  state.selectedIndex = Math.min(state.selectedIndex, state.objects.length - 1);
  refreshAllViews();
}

let lastPaintWorld = null;

function _stampForestCircle(wx, wz) {
  const fx = ((wx - WORLD_MIN) / WORLD_SIZE) * EFA_DIM;
  const fz = ((wz - WORLD_MIN) / WORLD_SIZE) * EFA_DIM;
  const r = Math.max(0.5, state.brushRadius - 0.5);
  const r2 = r * r;
  const x0 = Math.max(0, Math.floor(fx - r));
  const x1 = Math.min(EFA_DIM - 1, Math.ceil(fx + r));
  const z0 = Math.max(0, Math.floor(fz - r));
  const z1 = Math.min(EFA_DIM - 1, Math.ceil(fz + r));
  const val = state.brushErase ? 255 : 1;
  let changed = false;
  for (let z = z0; z <= z1; z++) {
    const dz = (z + 0.5) - fz;
    for (let x = x0; x <= x1; x++) {
      const dx = (x + 0.5) - fx;
      if (dx * dx + dz * dz <= r2) {
        const idx = z * EFA_DIM + x;
        if (state.efa[idx] !== val) {
          state.efa[idx] = val;
          changed = true;
        }
      }
    }
  }
  return changed;
}

function paintForestAt(wx, wz, isStart) {
  if (isStart) {
    pushUndo();
    lastPaintWorld = { x: wx, z: wz };
  }
  let changed = false;
  if (lastPaintWorld) {
    const dx = wx - lastPaintWorld.x;
    const dz = wz - lastPaintWorld.z;
    const dist = Math.hypot(dx, dz);
    const step = (WORLD_SIZE / EFA_DIM) * 0.4;
    const n = Math.max(1, Math.ceil(dist / step));
    for (let i = 1; i <= n; i++) {
      const t = i / n;
      if (_stampForestCircle(lastPaintWorld.x + dx * t, lastPaintWorld.z + dz * t)) {
        changed = true;
      }
    }
  } else {
    changed = _stampForestCircle(wx, wz);
  }
  lastPaintWorld = { x: wx, z: wz };
  if (changed) {
    vp3d.updateForestOverlay(state.efa);
    updateForestStats();
  }
}

function updateForestStats() {
  let cnt = 0;
  for (let i = 0; i < state.efa.length; i++) {
    if (state.efa[i] !== 255 && state.efa[i] !== 0) cnt++;
  }
  efaCountEl.textContent = String(cnt);
}

function updateInspector() {
  const o = state.objects[state.selectedIndex];
  if (!o) {
    noSelEl.classList.remove('hidden');
    objFieldsEl.classList.add('hidden');
    return;
  }
  noSelEl.classList.add('hidden');
  objFieldsEl.classList.remove('hidden');
  inspIdEl.textContent = `#${state.selectedIndex}`;
  inspModelEl.value = String(o.model_id);
  inspXEl.value = o.x.toFixed(2);
  inspYEl.value = o.y.toFixed(2);
  inspZEl.value = o.z.toFixed(2);
  const [gx, gz] = worldToTile(o.x, o.z);
  inspTileEl.textContent = `(${gx}, ${gz})`;
}

function refreshObjectList() {
  objCountEl.textContent = String(state.objects.length);
  const q = listSearchEl.value.trim().toLowerCase();
  tabObjectsEl.innerHTML = '';
  const frag = document.createDocumentFragment();
  for (let i = 0; i < state.objects.length; i++) {
    const o = state.objects[i];
    const label = `#${i}  Model ${o.model_id}`;
    if (q && !label.toLowerCase().includes(q)) continue;
    const item = document.createElement('div');
    item.className = 'bl-tree-item' + (i === state.selectedIndex ? ' selected' : '');
    item.dataset.idx = String(i);
    item.innerHTML = `<span>▣ #${i} · M${o.model_id}</span><span class="sub mono">${o.x.toFixed(0)}, ${o.z.toFixed(0)}</span>`;
    item.addEventListener('click', () => selectObject(i));
    item.addEventListener('dblclick', () => {
      selectObject(i);
      vp3d.focusWorldPoint(o.x, o.y, o.z, 90);
    });
    frag.appendChild(item);
  }
  tabObjectsEl.appendChild(frag);
}

function highlightListSelection() {
  for (const el of tabObjectsEl.children) {
    el.classList.toggle('selected', Number(el.dataset.idx) === state.selectedIndex);
  }
}

function refreshModelList() {
  mdlCountEl.textContent = String(state.models.length);
  const q = listSearchEl.value.trim().toLowerCase();
  tabModelsEl.innerHTML = '';
  const frag = document.createDocumentFragment();
  for (const m of state.models) {
    const label = `Model ${m.model_id}`;
    if (q && !label.toLowerCase().includes(q)) continue;
    const item = document.createElement('div');
    item.className = 'bl-tree-item' + (m.model_id === state.activeModelId ? ' selected' : '');
    item.innerHTML = `<span>△ Model #${m.model_id}</span><span class="sub mono">${m.tri_count} tris · ×${m.usage_count}</span>`;
    item.addEventListener('click', () => {
      state.activeModelId = m.model_id;
      placeModelEl.value = String(m.model_id);
      setTool('place');
      refreshModelList();
    });
    frag.appendChild(item);
  }
  tabModelsEl.appendChild(frag);
}

function populateModelSelects() {
  const opts = state.models
    .map((m) => `<option value="${m.model_id}">Model #${m.model_id} (${m.tri_count} tris)</option>`)
    .join('');
  inspModelEl.innerHTML = opts;
  placeModelEl.innerHTML = opts;
  if (state.models.length > 0) {
    state.activeModelId = state.models[0].model_id;
    placeModelEl.value = String(state.activeModelId);
  }
}

function refreshAllViews() {
  vp3d.rebuildObjects(state.objects, state.selectedIndex);
  vp3d.updateForestOverlay(state.efa);
  refreshObjectList();
  updateInspector();
  updateForestStats();
  selectObject(state.selectedIndex);
}

function setTool(tool) {
  state.activeTool = tool;
  vp3d.setTool(tool);
  modeSelect.value = tool;
  for (const btn of $('toolshelf').querySelectorAll('button[data-tool]')) {
    btn.classList.toggle('active', btn.dataset.tool === tool);
  }
  if (tool === 'forest') {
    setPropTab('tool');
    if (!state.layers.forest) {
      state.layers.forest = true;
      $('layer-forest').checked = true;
      vp3d.setLayers(state.layers);
    }
  }
}

function setShading(shade) {
  state.shading = shade;
  for (const b of $('shading-group').querySelectorAll('button')) {
    b.classList.toggle('active', b.dataset.shade === shade);
  }
  state.layers.wireframe = shade === 'wire';
  vp3d.setLayers(state.layers);
  if (state.mapName && (shade === 'hd' || shade === 'vanilla')) {
    const url = `/api/map/${state.mapName}/terrain_atlas.jpg?mode=${shade}&tile_px=128`;
    vp3d.setTerrainAtlas(url, state.atlasInfo);
  }
}

function setPropTab(tabKey) {
  const titles = { object: 'Object', tool: 'Active Tool & Brush', scene: 'Scene / Map' };
  $('prop-tab-title').textContent = titles[tabKey] || tabKey;
  for (const b of $('prop-tabs').querySelectorAll('button')) {
    b.classList.toggle('active', b.dataset.ptab === tabKey);
  }
  for (const page of document.querySelectorAll('.bl-prop-page')) {
    page.classList.toggle('hidden', page.id !== `ptab-${tabKey}`);
  }
}

// ============================================================================
// Map Load / Save / Revert
// ============================================================================

async function loadMapList() {
  const res = await fetch('/api/maps');
  const data = await res.json();
  state.maps = data.maps || [];
  mapSelect.innerHTML = state.maps
    .map((m) => `<option value="${m.name}">${m.name} (${m.object_count} obj${m.has_overrides ? ' • MOD' : ''})</option>`)
    .join('');
  const params = new URLSearchParams(window.location.search);
  const initial = params.get('map') || 'm02a';
  const found = state.maps.find((m) => m.name === initial) || state.maps[0];
  if (found) {
    mapSelect.value = found.name;
    await loadMap(found.name);
  }
}

async function loadMap(mapName) {
  setStatus(`Loading ${mapName} (513×513 terrain + Texture2DArray)…`);
  state.mapName = mapName;
  state.selectedIndex = -1;
  state.undoStack = [];
  state.redoStack = [];
  state.dirty = false;

  const [metaRes, hmRes, efaRes, mdlRes, tmRes] = await Promise.all([
    fetch(`/api/map/${mapName}/meta`),
    fetch(`/api/map/${mapName}/heightmap.bin?step=1`),
    fetch(`/api/map/${mapName}/efa.bin`),
    fetch(`/api/map/${mapName}/models.json`),
    fetch(`/api/map/${mapName}/tilemap.bin`),
  ]);

  if (!metaRes.ok) {
    toast(`Failed to load ${mapName}`, 'err');
    return;
  }

  state.meta = await metaRes.json();
  state.objects = state.meta.objects || [];
  state.models = state.meta.models || [];
  state.heightDim = Number(hmRes.headers.get('X-Grid-Dim') || '513');
  state.heightGrid = new Float32Array(await hmRes.arrayBuffer());
  state.efa = new Uint8Array(await efaRes.arrayBuffer());
  const mdlData = await mdlRes.json();

  state.atlasInfo = {
    cols: Number(tmRes.headers.get('X-Atlas-Cols') || '1'),
    tileCount: Number(tmRes.headers.get('X-Tile-Count') || '1'),
    tilePx: Number(tmRes.headers.get('X-Tile-Px') || '128'),
  };
  const tilemapU16 = new Uint16Array(await tmRes.arrayBuffer());

  $('info-hd').textContent = `${state.meta.hd_tiles} / ${state.meta.total_tiles}`;
  $('info-hscale').textContent = state.meta.height_scale.toFixed(2);
  $('info-ovr-path').textContent = `Output: ${state.meta.override_dir}`;

  const texMode = state.shading === 'vanilla' ? 'vanilla' : 'hd';
  const atlasUrl = `/api/map/${mapName}/terrain_atlas.jpg?mode=${texMode}&tile_px=128`;
  vp3d.buildTerrain(state.heightGrid, state.heightDim, tilemapU16, state.atlasInfo, atlasUrl);
  vp3d.loadModels(mdlData.models || []);
  vp3d.setLayers(state.layers);

  populateModelSelects();
  refreshModelList();
  refreshAllViews();
  updateUndoButtons();

  // Focus on an interesting central object if present (e.g. bridge #25 on m02a, else first object)
  if (state.objects.length > 0) {
    const o = state.objects[Math.min(25, state.objects.length - 1)];
    vp3d.focusWorldPoint(o.x, o.y, o.z, 220);
  } else {
    vp3d.focusWorldPoint(0, sampleTerrainY(0, 0), 0, 900);
  }

  setStatus(`Loaded ${mapName} — ${state.objects.length} objects · ${state.models.length} prefabs · ${state.meta.hd_tiles}/${state.meta.total_tiles} HD tiles`);
  toast(`Loaded ${mapName}`, 'ok');
}

async function saveCurrentMap() {
  if (!state.mapName) return;
  setStatus(`Saving ${state.mapName} to mods/maps/${state.mapName}/…`);
  const res = await fetch(`/api/map/${state.mapName}/save`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ objects: state.objects, efa_b64: uint8ToBase64(state.efa) }),
  });
  const data = await res.json();
  if (!res.ok) {
    toast(`Save failed: ${data.error}`, 'err');
    return;
  }
  state.dirty = false;
  if (state.meta) state.meta.has_overrides = true;
  updateUndoButtons();
  if (data.warnings && data.warnings.length > 0) {
    toast(`Saved with ${data.warnings.length} warning(s): ${data.warnings[0]}`, 'warn');
  } else {
    toast(`Saved ${state.mapName} (${data.object_count} obj) to mods/maps/${state.mapName}/`, 'ok');
  }
  setStatus(`Saved to ${data.override_dir}`);
}

async function revertCurrentMap() {
  if (!state.mapName) return;
  if (!confirm(`Delete all overrides in mods/maps/${state.mapName}/ and reload vanilla?`)) return;
  const res = await fetch(`/api/map/${state.mapName}/revert`, { method: 'POST' });
  if (res.ok) {
    toast(`Reverted ${state.mapName} to vanilla`, 'warn');
    await loadMap(state.mapName);
  }
}

// ============================================================================
// UI Event Bindings
// ============================================================================

// Topbar Menus
function closeAllMenus() {
  document.querySelectorAll('.bl-menu.open').forEach((m) => m.classList.remove('open'));
  $('overlays-popover').classList.add('hidden');
  closeContextMenu();
}

document.querySelectorAll('.bl-menu').forEach((menu) => {
  menu.querySelector('.bl-menu-trigger').addEventListener('click', (e) => {
    e.stopPropagation();
    const wasOpen = menu.classList.contains('open');
    closeAllMenus();
    if (!wasOpen) menu.classList.add('open');
  });
});

document.addEventListener('click', (e) => {
  if (!e.target.closest('.bl-menu') && !e.target.closest('#overlays-wrap') && !e.target.closest('#context-menu')) {
    closeAllMenus();
  }
});

document.querySelectorAll('.bl-dropdown button[data-action]').forEach((btn) => {
  btn.addEventListener('click', () => {
    const act = btn.dataset.action;
    closeAllMenus();
    if (act === 'save') saveCurrentMap();
    else if (act === 'revert') revertCurrentMap();
    else if (act === 'undo') $('btn-undo').click();
    else if (act === 'redo') $('btn-redo').click();
    else if (act === 'duplicate') duplicateSelected();
    else if (act === 'snap-y') snapSelectedY();
    else if (act === 'delete') deleteSelected();
    else if (act === 'toggle-proj') vp3d.toggleProjection();
    else if (act === 'view-top') vp3d.setViewPreset('top', true);
    else if (act === 'view-front') vp3d.setViewPreset('front', true);
    else if (act === 'view-right') vp3d.setViewPreset('right', true);
    else if (act === 'focus') focusSelected();
    else if (act === 'frame-all') vp3d.frameAll();
    else if (act === 'toggle-fly') vp3d.setFlyMode(!vp3d.flyMode);
    else if (act === 'toggle-toolbar') $('toolshelf').classList.toggle('hidden');
    else if (act === 'tool-place') setTool('place');
    else if (act === 'place-at-target') {
      const p = vp3d.getPivotWorldPos();
      placeObjectAt(p.x, null, p.z);
    }
  });
});

// Overlays Popover
$('btn-overlays').addEventListener('click', (e) => {
  e.stopPropagation();
  const pop = $('overlays-popover');
  const wasHidden = pop.classList.contains('hidden');
  closeAllMenus();
  if (wasHidden) pop.classList.remove('hidden');
});

for (const key of ['terrain', 'objects', 'forest', 'sectors']) {
  $(`layer-${key}`).addEventListener('change', (e) => {
    state.layers[key] = e.target.checked;
    vp3d.setLayers(state.layers);
  });
}

// Header & Shelf Controls
mapSelect.addEventListener('change', () => loadMap(mapSelect.value));
modeSelect.addEventListener('change', () => setTool(modeSelect.value));
$('shading-group').addEventListener('click', (e) => {
  const btn = e.target.closest('button');
  if (btn) setShading(btn.dataset.shade);
});
$('toolshelf').addEventListener('click', (e) => {
  const btn = e.target.closest('button[data-tool]');
  if (btn) setTool(btn.dataset.tool);
});
$('btn-shelf-snap').addEventListener('click', snapSelectedY);
$('btn-shelf-dup').addEventListener('click', duplicateSelected);
$('btn-shelf-del').addEventListener('click', deleteSelected);

// Nav Cluster Buttons
$('btn-nav-proj').addEventListener('click', () => vp3d.toggleProjection());
$('btn-nav-top').addEventListener('click', () => vp3d.setViewPreset('top', true));
$('btn-nav-focus').addEventListener('click', focusSelected);
$('btn-nav-home').addEventListener('click', () => vp3d.frameAll());

// Undo / Redo / Save / Revert
$('btn-undo').addEventListener('click', () => {
  if (state.undoStack.length === 0) return;
  state.redoStack.push(makeSnapshot());
  restoreSnapshot(state.undoStack.pop());
});
$('btn-redo').addEventListener('click', () => {
  if (state.redoStack.length === 0) return;
  state.undoStack.push(makeSnapshot());
  restoreSnapshot(state.redoStack.pop());
});
$('btn-save').addEventListener('click', saveCurrentMap);
$('btn-scene-save').addEventListener('click', saveCurrentMap);
$('btn-revert').addEventListener('click', revertCurrentMap);

// Outliner Tabs & Search
$('outliner-tabs').addEventListener('click', (e) => {
  const btn = e.target.closest('button');
  if (!btn) return;
  for (const b of $('outliner-tabs').querySelectorAll('button')) b.classList.toggle('active', b === btn);
  tabObjectsEl.classList.toggle('hidden', btn.dataset.tab !== 'objects');
  tabModelsEl.classList.toggle('hidden', btn.dataset.tab !== 'models');
});
listSearchEl.addEventListener('input', () => {
  refreshObjectList();
  refreshModelList();
});

// Properties Tabs
$('prop-tabs').addEventListener('click', (e) => {
  const btn = e.target.closest('button[data-ptab]');
  if (btn) setPropTab(btn.dataset.ptab);
});

// Inspector fields
inspModelEl.addEventListener('change', () => {
  const o = state.objects[state.selectedIndex];
  if (!o) return;
  pushUndo();
  o.model_id = Number(inspModelEl.value);
  refreshAllViews();
});
for (const [el, axis] of [[inspXEl, 'x'], [inspYEl, 'y'], [inspZEl, 'z']]) {
  el.addEventListener('change', () => {
    const o = state.objects[state.selectedIndex];
    if (!o) return;
    pushUndo();
    o[axis] = Number(el.value);
    vp3d.updateObjectTransform(state.selectedIndex, o.x, o.y, o.z);
    updateInspector();
    refreshObjectList();
  });
}
$('btn-snap-y').addEventListener('click', snapSelectedY);
$('btn-focus').addEventListener('click', focusSelected);
$('btn-duplicate').addEventListener('click', duplicateSelected);
$('btn-delete').addEventListener('click', deleteSelected);

// Placement & Brush
$('opt-autosnap').addEventListener('change', (e) => { state.autoSnapY = e.target.checked; });
placeModelEl.addEventListener('change', () => {
  state.activeModelId = Number(placeModelEl.value);
  refreshModelList();
});
$('brush-radius').addEventListener('input', (e) => {
  state.brushRadius = Number(e.target.value);
  $('brush-radius-val').textContent = String(state.brushRadius);
  vp3d.setBrushConfig(state.brushRadius, state.brushErase);
});
$('brush-mode-group').addEventListener('click', (e) => {
  const btn = e.target.closest('button');
  if (!btn) return;
  state.brushErase = btn.dataset.bmode === 'erase';
  for (const b of $('brush-mode-group').querySelectorAll('button')) b.classList.toggle('active', b === btn);
  vp3d.setBrushConfig(state.brushRadius, state.brushErase);
});

// Blender Keyboard Shortcuts
window.addEventListener('keydown', (e) => {
  if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA')) {
    return;
  }
  if ((e.ctrlKey || e.metaKey) && e.code === 'KeyZ') {
    e.preventDefault(); $('btn-undo').click();
  } else if ((e.ctrlKey || e.metaKey) && e.code === 'KeyY') {
    e.preventDefault(); $('btn-redo').click();
  } else if ((e.ctrlKey || e.metaKey) && e.code === 'KeyS') {
    e.preventDefault(); saveCurrentMap();
  } else if ((e.ctrlKey || e.metaKey || e.shiftKey) && e.code === 'KeyD') {
    e.preventDefault(); duplicateSelected();
  } else if (e.shiftKey && e.code === 'Backquote') {
    e.preventDefault(); vp3d.setFlyMode(!vp3d.flyMode);
  } else if (e.code === 'Delete') {
    deleteSelected();
  } else if (e.code === 'KeyT') {
    $('toolshelf').classList.toggle('hidden');
  } else if (e.code === 'Digit1') {
    setTool('select');
  } else if (e.code === 'Digit2') {
    setTool('place');
  } else if (e.code === 'Digit3') {
    setTool('forest');
  } else if (e.code === 'Numpad5') {
    e.preventDefault(); vp3d.toggleProjection();
  } else if (e.code === 'Numpad7') {
    e.preventDefault(); vp3d.setViewPreset(e.ctrlKey ? 'bottom' : 'top', true);
  } else if (e.code === 'Numpad1') {
    e.preventDefault(); vp3d.setViewPreset(e.ctrlKey ? 'back' : 'front', true);
  } else if (e.code === 'Numpad3') {
    e.preventDefault(); vp3d.setViewPreset(e.ctrlKey ? 'left' : 'right', true);
  } else if (e.code === 'NumpadDecimal' || e.code === 'KeyF') {
    focusSelected();
  } else if (e.code === 'Home') {
    e.preventDefault(); vp3d.frameAll();
  } else if (e.code === 'KeyG' && state.selectedIndex >= 0) {
    setTool('select'); vp3d.clearAxisLock();
    setStatus('Move mode — press X / Y / Z to lock axis, Esc to clear');
  } else if (e.code === 'KeyX' && state.selectedIndex >= 0) {
    const lock = vp3d.toggleAxisLock('x');
    setStatus(lock ? 'Axis lock: X' : 'Axis lock: Free');
  } else if (e.code === 'KeyY' && state.selectedIndex >= 0 && !e.ctrlKey && !e.metaKey) {
    const lock = vp3d.toggleAxisLock('y');
    setStatus(lock ? 'Axis lock: Y' : 'Axis lock: Free');
  } else if (e.code === 'KeyZ' && state.selectedIndex >= 0 && !e.ctrlKey && !e.metaKey) {
    const lock = vp3d.toggleAxisLock('z');
    setStatus(lock ? 'Axis lock: Z' : 'Axis lock: Free');
  } else if (e.code === 'Escape') {
    closeAllMenus();
    if (vp3d.flyMode) vp3d.setFlyMode(false);
    vp3d.clearAxisLock();
    setStatus('Ready');
  }
});

// Boot
loadMapList();
