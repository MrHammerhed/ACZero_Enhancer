import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';

export const SCENE_SCALE = 0.01;
const WORLD_SIZE = 524288.0;
const SCENE_WORLD_SIZE = WORLD_SIZE * SCENE_SCALE; // 5242.88

function colorForModel(modelId) {
  const hue = ((modelId * 137.508) % 360) / 360;
  return new THREE.Color().setHSL(hue, 0.55, 0.55);
}

const GIZMO_AXES = [
  { key: 'right',  label: 'X',  vec: new THREE.Vector3(1, 0, 0),  color: '#e6495c', positive: true },
  { key: 'left',   label: '-X', vec: new THREE.Vector3(-1, 0, 0), color: '#8a323e', positive: false },
  { key: 'top',    label: 'Y',  vec: new THREE.Vector3(0, 1, 0),  color: '#7db838', positive: true },
  { key: 'bottom', label: '-Y', vec: new THREE.Vector3(0, -1, 0), color: '#4d6f26', positive: false },
  { key: 'front',  label: 'Z',  vec: new THREE.Vector3(0, 0, 1),  color: '#3d8ef7', positive: true },
  { key: 'back',   label: '-Z', vec: new THREE.Vector3(0, 0, -1), color: '#295794', positive: false },
];

export class Viewport3D {
  constructor(hostEl, navGizmoCanvas, callbacks) {
    this.hostEl = hostEl;
    this.navCanvas = navGizmoCanvas;
    this.navCtx = navGizmoCanvas ? navGizmoCanvas.getContext('2d') : null;
    this.navProjected = [];
    this.cb = callbacks;

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x161920);
    this.scene.fog = new THREE.FogExp2(0x161920, 0.00010);

    const w = Math.max(1, hostEl.clientWidth);
    const h = Math.max(1, hostEl.clientHeight);
    const aspect = w / h;

    this.perspCamera = new THREE.PerspectiveCamera(50, aspect, 0.5, 30000);
    this.perspCamera.position.set(0, 600, 1200);

    const frustumHalf = 600;
    this.orthoCamera = new THREE.OrthographicCamera(
      -frustumHalf * aspect, frustumHalf * aspect,
      frustumHalf, -frustumHalf,
      -20000, 30000
    );
    this.orthoCamera.position.copy(this.perspCamera.position);

    this.isOrtho = false;
    this.viewPresetName = 'User';
    this.camera = this.perspCamera;

    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.setSize(w, h);
    hostEl.appendChild(this.renderer.domElement);

    // Lighting
    this.scene.add(new THREE.HemisphereLight(0xdbeaff, 0x2c323e, 0.95));
    const sun = new THREE.DirectionalLight(0xfff5e2, 1.15);
    sun.position.set(1600, 2400, 1100);
    this.scene.add(sun);

    // Blender-style OrbitControls:
    //   MMB = Orbit, Shift+MMB = Pan, Ctrl+MMB / Wheel = Zoom
    //   LMB = Select / Place / Brush, RMB = Context Menu (Alt+RMB or Shift+~ = Fly)
    this.orbit = new OrbitControls(this.camera, this.renderer.domElement);
    this.orbit.enableDamping = true;
    this.orbit.dampingFactor = 0.09;
    this.orbit.screenSpacePanning = true;
    this.orbit.zoomSpeed = 1.25;
    this.orbit.maxDistance = 12000;
    this.orbit.minDistance = 2;
    this.orbit.mouseButtons = {
      LEFT: null,
      MIDDLE: THREE.MOUSE.ROTATE,
      RIGHT: null,
    };

    this.orbit.addEventListener('start', () => {
      this.isOrbiting = true;
      if (this.orbit.mouseButtons.MIDDLE === THREE.MOUSE.ROTATE) {
        if (this.viewPresetName !== 'User') {
          this.viewPresetName = 'User';
          this._notifyViewChanged();
        }
      }
    });
    this.orbit.addEventListener('end', () => {
      this.isOrbiting = false;
    });

    this.transform = new TransformControls(this.camera, this.renderer.domElement);
    this.transform.setMode('translate');
    this.transform.setSize(0.85);
    this.scene.add(this.transform);

    this.axisLock = null;
    this.isDraggingGizmo = false;
    this.transform.addEventListener('dragging-changed', (ev) => {
      this.orbit.enabled = !ev.value;
      this.isDraggingGizmo = ev.value;
      if (ev.value) this.cb.onGizmoDragStart?.();
      else this.cb.onGizmoDragEnd?.();
    });
    this.transform.addEventListener('objectChange', () => {
      if (!this.isDraggingGizmo || !this.transform.object) return;
      const obj = this.transform.object;
      const idx = obj.userData.objIndex;
      if (idx === undefined) return;
      this.cb.onObjectDragged?.(
        idx,
        obj.position.x / SCENE_SCALE,
        obj.position.y / SCENE_SCALE,
        obj.position.z / SCENE_SCALE
      );
    });

    // Terrain shader resources (WebGL2 Texture2DArray — zero mip bleeding!)
    this.terrainMesh = null;
    this.heightGridF32 = null;
    this.heightDim = 0;
    this.heightMinScene = -10;
    this.heightMaxScene = 500;

    const dummyArray = new THREE.DataArrayTexture(new Uint8Array([70, 82, 70, 255]), 1, 1, 1);
    dummyArray.needsUpdate = true;
    this.tileArrayTex = dummyArray;
    this.tileMapTex = null;

    // Forest overlay texture (128x128 RGBA) rendered directly inside terrain shader (zero Z-flip, zero extra mesh)
    this.forestCanvas = document.createElement('canvas');
    this.forestCanvas.width = 128;
    this.forestCanvas.height = 128;
    this.forestTex = new THREE.CanvasTexture(this.forestCanvas);
    this.forestTex.magFilter = THREE.NearestFilter;
    this.forestTex.minFilter = THREE.NearestFilter;
    this.forestTex.flipY = false;

    this.terrainUniforms = {
      uTileMap: { value: null },
      uTileArray: { value: dummyArray },
      uTileCount: { value: 1.0 },
      uForestTex: { value: this.forestTex },
      uShowForest: { value: 0.0 },
      uBrushUv: { value: new THREE.Vector2(-10, -10) },
      uBrushRadiusUv: { value: 0.0 },
      uBrushColor: { value: new THREE.Color(0x4caf50) },
    };

    this.sectorGrid = this._buildSectorGrid();
    this.scene.add(this.sectorGrid);

    this.objectsGroup = new THREE.Group();
    this.scene.add(this.objectsGroup);

    this.modelGeometries = new Map();
    this.objectMeshes = [];
    this.selectedIndex = -1;

    // Input & Fly state
    this.raycaster = new THREE.Raycaster();
    this.pointer = new THREE.Vector2();
    this.keys = new Set();
    this.activeTool = 'select';
    this.brushRadiusCells = 2;
    this.brushErase = false;
    this.isOrbiting = false;
    this.isPaintingForest = false;
    this.flyMode = false;       // Shift+~ toggle mode
    this.isAltRmbFly = false;   // Alt+RMB temporary hold mode
    this.flyLast = { x: 0, y: 0 };
    this.flyEuler = new THREE.Euler(0, 0, 0, 'YXZ');
    this.pointerDownPos = { x: 0, y: 0 };
    this._lastHoverTime = 0;
    this._gizmoDirty = true;
    this._lastGizmoQuat = new THREE.Quaternion();

    this._bindEvents();
    this._lastTime = performance.now();
    this._animate();
  }

  _buildSectorGrid() {
    const grid = new THREE.GridHelper(SCENE_WORLD_SIZE, 16, 0x60a5fa, 0x3b82f6);
    grid.position.y = 0.5;
    grid.material.opacity = 0.22;
    grid.material.transparent = true;
    return grid;
  }

  _notifyViewChanged() {
    this.cb.onViewChanged?.({
      isOrtho: this.isOrtho,
      preset: this.viewPresetName,
      flyMode: this.flyMode,
    });
  }

  setFlyMode(enabled) {
    if (this.flyMode === enabled) return;
    this.flyMode = enabled;
    if (enabled) {
      if (this.isOrtho) this.setProjection(false);
      this.flyEuler.setFromQuaternion(this.camera.quaternion, 'YXZ');
      this.orbit.enabled = false;
      this.renderer.domElement.style.cursor = 'crosshair';
    } else {
      this.orbit.enabled = !this.isDraggingGizmo;
      this.renderer.domElement.style.cursor = '';
    }
    this._notifyViewChanged();
  }

  setProjection(ortho) {
    if (this.isOrtho === ortho) return;
    const w = Math.max(1, this.hostEl.clientWidth);
    const h = Math.max(1, this.hostEl.clientHeight);
    const aspect = w / h;
    const target = this.orbit.target.clone();
    const fovRad = THREE.MathUtils.degToRad(this.perspCamera.fov);

    if (ortho) {
      const dist = Math.max(10, this.perspCamera.position.distanceTo(target));
      const halfH = dist * Math.tan(fovRad * 0.5);
      this.orthoCamera.position.copy(this.perspCamera.position);
      this.orthoCamera.quaternion.copy(this.perspCamera.quaternion);
      this.orthoCamera.left = -halfH * aspect;
      this.orthoCamera.right = halfH * aspect;
      this.orthoCamera.top = halfH;
      this.orthoCamera.bottom = -halfH;
      this.orthoCamera.zoom = 1.0;
      this.orthoCamera.updateProjectionMatrix();
      this.camera = this.orthoCamera;
      this.isOrtho = true;
      this.scene.fog = null; // Disable fog in Ortho map view for crystal-clear top-down visibility
    } else {
      const halfH = this.orthoCamera.top / Math.max(0.001, this.orthoCamera.zoom);
      const dist = Math.max(10, Math.min(11000, halfH / Math.tan(fovRad * 0.5)));
      const dir = new THREE.Vector3().subVectors(this.orthoCamera.position, target);
      if (dir.lengthSq() < 1e-6) dir.set(0, 1, 0.01);
      dir.normalize();
      this.perspCamera.position.copy(target).addScaledVector(dir, dist);
      this.perspCamera.quaternion.copy(this.orthoCamera.quaternion);
      this.perspCamera.aspect = aspect;
      this.perspCamera.updateProjectionMatrix();
      this.camera = this.perspCamera;
      this.isOrtho = false;
      this.scene.fog = new THREE.FogExp2(0x161920, 0.00010);
    }

    this.orbit.object = this.camera;
    this.orbit.target.copy(target);
    this.orbit.update();
    this.transform.camera = this.camera;
    this._notifyViewChanged();
  }

  toggleProjection() {
    this.setProjection(!this.isOrtho);
  }

  setViewPreset(preset, forceOrtho = true) {
    if (this.flyMode) this.setFlyMode(false);
    const target = this.orbit.target.clone();
    const dist = Math.max(80, this.camera.position.distanceTo(target));
    const labels = {
      top: 'Top', bottom: 'Bottom', front: 'Front', back: 'Back', right: 'Right', left: 'Left',
    };
    this.viewPresetName = labels[preset] || 'User';

    if (preset === 'top') {
      this.camera.position.set(target.x, target.y + dist, target.z + 0.01);
    } else if (preset === 'bottom') {
      this.camera.position.set(target.x, target.y - dist, target.z + 0.01);
    } else if (preset === 'front') {
      this.camera.position.set(target.x, target.y, target.z + dist);
    } else if (preset === 'back') {
      this.camera.position.set(target.x, target.y, target.z - dist);
    } else if (preset === 'right') {
      this.camera.position.set(target.x + dist, target.y, target.z);
    } else if (preset === 'left') {
      this.camera.position.set(target.x - dist, target.y, target.z);
    }
    this.camera.lookAt(target);
    this.orbit.update();
    if (forceOrtho && !this.isOrtho) {
      this.setProjection(true);
    } else {
      this._notifyViewChanged();
    }
  }

  frameAll() {
    if (this.flyMode) this.setFlyMode(false);
    this.orbit.target.set(0, 0, 0);
    this.viewPresetName = 'Top';
    this.perspCamera.position.set(0, 5600, 0.01);
    this.perspCamera.lookAt(0, 0, 0);
    if (this.isOrtho) {
      const w = Math.max(1, this.hostEl.clientWidth);
      const h = Math.max(1, this.hostEl.clientHeight);
      const aspect = w / h;
      const halfH = SCENE_WORLD_SIZE * 0.54;
      this.orthoCamera.position.set(0, 6000, 0.01);
      this.orthoCamera.left = -halfH * aspect;
      this.orthoCamera.right = halfH * aspect;
      this.orthoCamera.top = halfH;
      this.orthoCamera.bottom = -halfH;
      this.orthoCamera.zoom = 1.0;
      this.orthoCamera.lookAt(0, 0, 0);
      this.orthoCamera.updateProjectionMatrix();
    }
    this.orbit.update();
    this._notifyViewChanged();
  }

  _bindEvents() {
    const dom = this.renderer.domElement;
    dom.addEventListener('contextmenu', (e) => e.preventDefault());

    // Capture phase for MMB Blender modifiers + Alt+RMB Fly + Context menu prep
    dom.addEventListener(
      'pointerdown',
      (e) => {
        if (this.flyMode) {
          this.setFlyMode(false);
          return;
        }
        if (e.button === 1) {
          if (e.shiftKey) this.orbit.mouseButtons.MIDDLE = THREE.MOUSE.PAN;
          else if (e.ctrlKey) this.orbit.mouseButtons.MIDDLE = THREE.MOUSE.DOLLY;
          else this.orbit.mouseButtons.MIDDLE = THREE.MOUSE.ROTATE;
          return;
        }

        this.pointerDownPos = { x: e.clientX, y: e.clientY };

        if (e.button === 2 && e.altKey) {
          this.isAltRmbFly = true;
          if (this.isOrtho) this.setProjection(false);
          this.flyLast = { x: e.clientX, y: e.clientY };
          this.flyEuler.setFromQuaternion(this.camera.quaternion, 'YXZ');
          this.orbit.enabled = false;
          dom.style.cursor = 'grabbing';
          return;
        }

        if (this.activeTool === 'forest' && e.button === 0 && !e.altKey) {
          this.isPaintingForest = true;
          this._paintForestAtEvent(e, true);
        }
      },
      { capture: true }
    );

    window.addEventListener('pointermove', (e) => {
      if (this.flyMode || this.isAltRmbFly) {
        const dx = this.flyMode ? e.movementX : (e.clientX - this.flyLast.x);
        const dy = this.flyMode ? e.movementY : (e.clientY - this.flyLast.y);
        this.flyLast = { x: e.clientX, y: e.clientY };
        this.flyEuler.y -= dx * 0.0028;
        this.flyEuler.x = Math.max(-Math.PI * 0.49, Math.min(Math.PI * 0.49, this.flyEuler.x - dy * 0.0028));
        this.camera.quaternion.setFromEuler(this.flyEuler);
        const dir = new THREE.Vector3(0, 0, -1).applyQuaternion(this.camera.quaternion);
        const dist = Math.max(40, this.camera.position.distanceTo(this.orbit.target));
        this.orbit.target.copy(this.camera.position).addScaledVector(dir, dist);
        return;
      }

      if (e.target === dom) {
        // Never raycast while orbiting/panning/dragging gizmo — keeps rotation 100% silky smooth
        if (this.isOrbiting || this.isDraggingGizmo || (e.buttons & 4)) {
          return;
        }
        const now = performance.now();
        if (!this.isPaintingForest && this.activeTool !== 'forest' && now - this._lastHoverTime < 40) {
          return;
        }
        this._lastHoverTime = now;

        const hit = this._raycastTerrain(e);
        if (hit) {
          const wx = hit.x / SCENE_SCALE;
          const wy = hit.y / SCENE_SCALE;
          const wz = hit.z / SCENE_SCALE;
          this.cb.onHoverWorld?.(wx, wy, wz);
          if (this.activeTool === 'forest') {
            this.terrainUniforms.uBrushUv.value.set(
              (wx + WORLD_SIZE * 0.5) / WORLD_SIZE,
              (wz + WORLD_SIZE * 0.5) / WORLD_SIZE
            );
            this.terrainUniforms.uBrushRadiusUv.value = Math.max(0.5, this.brushRadiusCells - 0.5) / 128.0;
          } else {
            this.terrainUniforms.uBrushRadiusUv.value = 0.0;
          }
          if (this.isPaintingForest && this.activeTool === 'forest') {
            this.cb.onPaintForest?.(wx, wz, false);
          }
        } else {
          this.terrainUniforms.uBrushRadiusUv.value = 0.0;
        }
      }
    });

    dom.addEventListener('pointerleave', () => {
      this.terrainUniforms.uBrushRadiusUv.value = 0.0;
    });

    window.addEventListener('pointerup', (e) => {
      if (e.button === 2 && this.isAltRmbFly) {
        this.isAltRmbFly = false;
        this.orbit.enabled = !this.isDraggingGizmo;
        dom.style.cursor = '';
        return;
      }
      if (e.button === 2 && e.target === dom && !e.altKey) {
        const d = Math.hypot(e.clientX - this.pointerDownPos.x, e.clientY - this.pointerDownPos.y);
        if (d < 6) {
          const hitObj = this._raycastObjectIdx(e);
          const hitPt = this._raycastTerrain(e);
          const worldPt = hitPt
            ? { x: hitPt.x / SCENE_SCALE, y: hitPt.y / SCENE_SCALE, z: hitPt.z / SCENE_SCALE }
            : null;
          this.cb.onContextMenu?.(e.clientX, e.clientY, hitObj, worldPt);
        }
      }
      if (this.isPaintingForest) {
        this.isPaintingForest = false;
        this.cb.onForestStrokeEnd?.();
      }
    });

    dom.addEventListener('click', (e) => {
      if (e.button !== 0 || this.isDraggingGizmo) return;
      const d = Math.hypot(e.clientX - this.pointerDownPos.x, e.clientY - this.pointerDownPos.y);
      if (d > 5) return;

      if (this.activeTool === 'select') {
        const idx = this._raycastObjectIdx(e);
        this.cb.onSelectObject?.(idx);
      } else if (this.activeTool === 'place') {
        const hit = this._raycastTerrain(e);
        if (hit) {
          this.cb.onPlaceObject?.(hit.x / SCENE_SCALE, hit.y / SCENE_SCALE, hit.z / SCENE_SCALE);
        }
      }
    });

    if (this.navCanvas) {
      this.navCanvas.addEventListener('click', (e) => {
        const rect = this.navCanvas.getBoundingClientRect();
        const cx = (e.clientX - rect.left) * (this.navCanvas.width / rect.width);
        const cy = (e.clientY - rect.top) * (this.navCanvas.height / rect.height);
        for (let i = this.navProjected.length - 1; i >= 0; i--) {
          const p = this.navProjected[i];
          if (Math.hypot(cx - p.x, cy - p.y) <= p.r + 3) {
            this.setViewPreset(p.key, true);
            return;
          }
        }
      });
    }

    window.addEventListener('keydown', (e) => {
      if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA')) {
        return;
      }
      this.keys.add(e.code);
    });
    window.addEventListener('keyup', (e) => {
      this.keys.delete(e.code);
    });

    new ResizeObserver(() => this.resize()).observe(this.hostEl);
  }

  _updatePointer(e) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    this.pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
  }

  _sampleHeightScene(sx, sz) {
    if (!this.heightGridF32 || this.heightDim < 2) return 0.0;
    const dim = this.heightDim;
    const u = (sx + SCENE_WORLD_SIZE * 0.5) / SCENE_WORLD_SIZE;
    const v = (sz + SCENE_WORLD_SIZE * 0.5) / SCENE_WORLD_SIZE;
    if (u < 0 || u > 1 || v < 0 || v > 1) return 0.0;
    const gx = u * (dim - 1);
    const gz = v * (dim - 1);
    const ix = Math.min(dim - 2, Math.floor(gx));
    const iz = Math.min(dim - 2, Math.floor(gz));
    const tx = gx - ix;
    const tz = gz - iz;
    const g = this.heightGridF32;
    const h00 = g[iz * dim + ix];
    const h10 = g[iz * dim + ix + 1];
    const h01 = g[(iz + 1) * dim + ix];
    const h11 = g[(iz + 1) * dim + ix + 1];
    const wy =
      h00 * (1 - tx) * (1 - tz) +
      h10 * tx * (1 - tz) +
      h01 * (1 - tx) * tz +
      h11 * tx * tz;
    return wy * SCENE_SCALE;
  }

  /**
   * Fast analytical heightfield raymarch (~0.02 ms vs ~80 ms CPU triangle BVH scan of 524k tris).
   */
  _raycastTerrain(e) {
    if (!this.heightGridF32) return null;
    this._updatePointer(e);
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const ray = this.raycaster.ray;
    const half = SCENE_WORLD_SIZE * 0.5;
    const box = new THREE.Box3(
      new THREE.Vector3(-half, this.heightMinScene - 5, -half),
      new THREE.Vector3(half, this.heightMaxScene + 5, half)
    );

    // Intersect ray with terrain bounding box [tMin, tMax]
    let tMin = -Infinity;
    let tMax = Infinity;
    for (const axis of ['x', 'y', 'z']) {
      const o = ray.origin[axis];
      const d = ray.direction[axis];
      if (Math.abs(d) < 1e-8) {
        if (o < box.min[axis] || o > box.max[axis]) return null;
      } else {
        let t1 = (box.min[axis] - o) / d;
        let t2 = (box.max[axis] - o) / d;
        if (t1 > t2) { const tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tMin) tMin = t1;
        if (t2 < tMax) tMax = t2;
        if (tMin > tMax) return null;
      }
    }
    if (tMax < 0) return null;
    const tStart = Math.max(0, tMin);
    const tEnd = tMax;
    const steps = 96;
    const dt = (tEnd - tStart) / steps;

    let prevT = tStart;
    let prevDiff = (ray.origin.y + ray.direction.y * tStart) - this._sampleHeightScene(
      ray.origin.x + ray.direction.x * tStart,
      ray.origin.z + ray.direction.z * tStart
    );

    for (let i = 1; i <= steps; i++) {
      const t = tStart + dt * i;
      const px = ray.origin.x + ray.direction.x * t;
      const py = ray.origin.y + ray.direction.y * t;
      const pz = ray.origin.z + ray.direction.z * t;
      const diff = py - this._sampleHeightScene(px, pz);
      if (diff <= 0) {
        // Binary refinement (10 iterations -> sub-unit accuracy)
        let lo = prevT;
        let hi = t;
        for (let k = 0; k < 10; k++) {
          const mid = 0.5 * (lo + hi);
          const mx = ray.origin.x + ray.direction.x * mid;
          const my = ray.origin.y + ray.direction.y * mid;
          const mz = ray.origin.z + ray.direction.z * mid;
          if (my - this._sampleHeightScene(mx, mz) > 0) lo = mid;
          else hi = mid;
        }
        const ft = 0.5 * (lo + hi);
        const fx = ray.origin.x + ray.direction.x * ft;
        const fz = ray.origin.z + ray.direction.z * ft;
        return new THREE.Vector3(fx, this._sampleHeightScene(fx, fz), fz);
      }
      prevT = t;
      prevDiff = diff;
    }

    // Fallback: flat intersection at y=0 if ray grazed edge
    if (Math.abs(ray.direction.y) > 1e-6) {
      const t0 = -ray.origin.y / ray.direction.y;
      if (t0 >= 0) {
        const fx = ray.origin.x + ray.direction.x * t0;
        const fz = ray.origin.z + ray.direction.z * t0;
        if (Math.abs(fx) <= half && Math.abs(fz) <= half) {
          return new THREE.Vector3(fx, this._sampleHeightScene(fx, fz), fz);
        }
      }
    }
    return null;
  }

  _raycastObjectIdx(e) {
    this._updatePointer(e);
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const hits = this.raycaster.intersectObjects(this.objectMeshes, false);
    return hits.length > 0 ? hits[0].object.userData.objIndex : -1;
  }

  _paintForestAtEvent(e, isStart) {
    const hit = this._raycastTerrain(e);
    if (hit) {
      this.cb.onPaintForest?.(hit.x / SCENE_SCALE, hit.z / SCENE_SCALE, isStart);
    }
  }

  resize() {
    const w = Math.max(1, this.hostEl.clientWidth);
    const h = Math.max(1, this.hostEl.clientHeight);
    const aspect = w / h;
    this.perspCamera.aspect = aspect;
    this.perspCamera.updateProjectionMatrix();
    const halfH = this.orthoCamera.top;
    this.orthoCamera.left = -halfH * aspect;
    this.orthoCamera.right = halfH * aspect;
    this.orthoCamera.updateProjectionMatrix();
    this.renderer.setSize(w, h);
  }

  setTool(tool) {
    this.activeTool = tool;
    this.transform.visible = tool === 'select' && this.selectedIndex >= 0;
    this.transform.enabled = tool === 'select' && this.selectedIndex >= 0;
    if (tool !== 'forest') {
      this.terrainUniforms.uBrushRadiusUv.value = 0.0;
    }
  }

  setBrushConfig(radiusCells, erase) {
    this.brushRadiusCells = radiusCells;
    this.brushErase = erase;
    this.terrainUniforms.uBrushColor.value.setHex(erase ? 0xef4444 : 0x4caf50);
    if (this.activeTool === 'forest' && this.terrainUniforms.uBrushRadiusUv.value > 0) {
      this.terrainUniforms.uBrushRadiusUv.value = Math.max(0.5, radiusCells - 0.5) / 128.0;
    }
  }

  toggleAxisLock(axis) {
    this.axisLock = this.axisLock === axis ? null : axis;
    this.transform.showX = !this.axisLock || this.axisLock === 'x';
    this.transform.showY = !this.axisLock || this.axisLock === 'y';
    this.transform.showZ = !this.axisLock || this.axisLock === 'z';
    return this.axisLock;
  }

  clearAxisLock() {
    this.axisLock = null;
    this.transform.showX = true;
    this.transform.showY = true;
    this.transform.showZ = true;
  }

  setLayers(layers) {
    if (this.terrainMesh) {
      this.terrainMesh.visible = layers.terrain;
      this.terrainMesh.material.wireframe = layers.wireframe;
    }
    this.terrainUniforms.uShowForest.value = layers.forest ? 1.0 : 0.0;
    this.objectsGroup.visible = layers.objects;
    this.sectorGrid.visible = layers.sectors;
  }

  buildTerrain(heightGridF32, gridDim, tilemapU16, atlasInfo, atlasUrl) {
    if (this.terrainMesh) {
      this.scene.remove(this.terrainMesh);
      this.terrainMesh.geometry.dispose();
      this.terrainMesh.material.dispose();
      this.terrainMesh = null;
    }
    if (this.tileMapTex) {
      this.tileMapTex.dispose();
      this.tileMapTex = null;
    }

    this.heightGridF32 = heightGridF32;
    this.heightDim = gridDim;
    let minH = Infinity;
    let maxH = -Infinity;
    for (let i = 0; i < heightGridF32.length; i++) {
      const v = heightGridF32[i];
      if (v < minH) minH = v;
      if (v > maxH) maxH = v;
    }
    this.heightMinScene = (Number.isFinite(minH) ? minH : 0) * SCENE_SCALE;
    this.heightMaxScene = (Number.isFinite(maxH) ? maxH : 1000) * SCENE_SCALE;

    // Encode 256x256 uint16 tile IDs into RGBA8 DataTexture
    const rgba = new Uint8Array(256 * 256 * 4);
    for (let i = 0; i < 65536; i++) {
      const tid = tilemapU16[i] || 0;
      rgba[i * 4 + 0] = tid & 0xff;
      rgba[i * 4 + 1] = (tid >> 8) & 0xff;
      rgba[i * 4 + 2] = 0;
      rgba[i * 4 + 3] = 255;
    }
    this.tileMapTex = new THREE.DataTexture(rgba, 256, 256, THREE.RGBAFormat);
    this.tileMapTex.magFilter = THREE.NearestFilter;
    this.tileMapTex.minFilter = THREE.NearestFilter;
    this.tileMapTex.flipY = false;
    this.tileMapTex.generateMipmaps = false;
    this.tileMapTex.needsUpdate = true;

    this.terrainUniforms.uTileMap.value = this.tileMapTex;
    this.terrainUniforms.uTileCount.value = Math.max(1, atlasInfo.tileCount);

    const segs = gridDim - 1;
    const geo = new THREE.PlaneGeometry(SCENE_WORLD_SIZE, SCENE_WORLD_SIZE, segs, segs);
    geo.rotateX(-Math.PI / 2);

    const pos = geo.attributes.position;
    for (let iz = 0; iz < gridDim; iz++) {
      for (let ix = 0; ix < gridDim; ix++) {
        const vi = iz * gridDim + ix;
        pos.setY(vi, heightGridF32[vi] * SCENE_SCALE);
      }
    }
    pos.needsUpdate = true;
    geo.computeVertexNormals();

    // 1x1 placeholder map enables USE_MAP + vMapUv in MeshStandardMaterial
    const placeholderTex = new THREE.DataTexture(new Uint8Array([255, 255, 255, 255]), 1, 1, THREE.RGBAFormat);
    placeholderTex.needsUpdate = true;

    const mat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      map: placeholderTex,
      roughness: 0.92,
      metalness: 0.0,
    });

    const uniformsRef = this.terrainUniforms;
    mat.onBeforeCompile = (shader) => {
      shader.uniforms.uTileMap = uniformsRef.uTileMap;
      shader.uniforms.uTileArray = uniformsRef.uTileArray;
      shader.uniforms.uTileCount = uniformsRef.uTileCount;
      shader.uniforms.uForestTex = uniformsRef.uForestTex;
      shader.uniforms.uShowForest = uniformsRef.uShowForest;
      shader.uniforms.uBrushUv = uniformsRef.uBrushUv;
      shader.uniforms.uBrushRadiusUv = uniformsRef.uBrushRadiusUv;
      shader.uniforms.uBrushColor = uniformsRef.uBrushColor;

      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <map_pars_fragment>',
        `#include <map_pars_fragment>
        uniform sampler2D uTileMap;
        uniform highp sampler2DArray uTileArray;
        uniform float uTileCount;
        uniform sampler2D uForestTex;
        uniform float uShowForest;
        uniform vec2 uBrushUv;
        uniform float uBrushRadiusUv;
        uniform vec3 uBrushColor;`
      );
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <map_fragment>',
        `#ifdef USE_MAP
          vec2 aczMapUv = vec2(vMapUv.x, 1.0 - vMapUv.y);
          vec2 tileCoord = aczMapUv * 256.0;
          vec2 cell = floor(clamp(tileCoord, vec2(0.0), vec2(255.999)));
          vec4 idxSample = texture2D(uTileMap, (cell + 0.5) / 256.0);
          float tid = floor(idxSample.r * 255.0 + 0.5) + floor(idxSample.g * 255.0 + 0.5) * 256.0;
          tid = clamp(tid, 0.0, max(0.0, uTileCount - 1.0));
          vec2 localUv = fract(tileCoord);
          vec4 sampledDiffuseColor = textureGrad(uTileArray, vec3(localUv, tid), dFdx(tileCoord), dFdy(tileCoord));
          diffuseColor *= sampledDiffuseColor;

          // Forest .efa overlay sampled in identical aczMapUv space (100% cursor-aligned)
          if (uShowForest > 0.5) {
            vec4 fCol = texture2D(uForestTex, aczMapUv);
            diffuseColor.rgb = mix(diffuseColor.rgb, fCol.rgb, fCol.a * 0.65);
          }

          // Live 3D brush cursor ring projected onto terrain
          if (uBrushRadiusUv > 0.0) {
            float d = distance(aczMapUv, uBrushUv);
            float ringW = max(0.0012, fwidth(d) * 2.0);
            float ring = 1.0 - smoothstep(0.0, ringW, abs(d - uBrushRadiusUv));
            float fill = d < uBrushRadiusUv ? 0.14 : 0.0;
            diffuseColor.rgb = mix(diffuseColor.rgb, uBrushColor, clamp(ring * 0.9 + fill, 0.0, 0.95));
          }
        #endif`
      );
    };

    this.terrainMesh = new THREE.Mesh(geo, mat);
    this.scene.add(this.terrainMesh);

    this.setTerrainAtlas(atlasUrl, atlasInfo);
  }

  /**
   * Loads the compact JPEG atlas and slices it client-side into a WebGL2 THREE.DataArrayTexture
   * so every unique terrain tile has isolated per-layer mipmaps (0% cross-tile mip bleeding).
   */
  setTerrainAtlas(url, atlasInfo) {
    if (!this.terrainMesh) return;
    const img = new Image();
    img.crossOrigin = 'anonymous';
    img.onload = () => {
      const cols = Math.max(1, atlasInfo.cols);
      const tilePx = Math.max(32, atlasInfo.tilePx || Math.floor(img.width / cols));
      const tileCount = Math.max(1, atlasInfo.tileCount);

      const canvas = document.createElement('canvas');
      canvas.width = img.width;
      canvas.height = img.height;
      const ctx = canvas.getContext('2d', { willReadFrequently: true });
      ctx.drawImage(img, 0, 0);
      const src = ctx.getImageData(0, 0, img.width, img.height).data;

      const rowBytes = tilePx * 4;
      const atlasRowBytes = img.width * 4;
      const layerBytes = tilePx * tilePx * 4;
      const arrData = new Uint8Array(layerBytes * tileCount);

      for (let tid = 0; tid < tileCount; tid++) {
        const col = tid % cols;
        const row = Math.floor(tid / cols);
        const srcX0 = col * tilePx;
        const srcY0 = row * tilePx;
        const dstLayerOff = tid * layerBytes;
        for (let y = 0; y < tilePx; y++) {
          const srcOff = (srcY0 + y) * atlasRowBytes + srcX0 * 4;
          arrData.set(src.subarray(srcOff, srcOff + rowBytes), dstLayerOff + y * rowBytes);
        }
      }

      const arrayTex = new THREE.DataArrayTexture(arrData, tilePx, tilePx, tileCount);
      arrayTex.format = THREE.RGBAFormat;
      arrayTex.type = THREE.UnsignedByteType;
      arrayTex.colorSpace = THREE.SRGBColorSpace;
      arrayTex.wrapS = THREE.ClampToEdgeWrapping;
      arrayTex.wrapT = THREE.ClampToEdgeWrapping;
      arrayTex.minFilter = THREE.LinearMipmapLinearFilter;
      arrayTex.magFilter = THREE.LinearFilter;
      arrayTex.anisotropy = Math.min(8, this.renderer.capabilities.getMaxAnisotropy());
      arrayTex.generateMipmaps = true;
      arrayTex.needsUpdate = true;

      if (this.tileArrayTex) this.tileArrayTex.dispose();
      this.tileArrayTex = arrayTex;
      this.terrainUniforms.uTileArray.value = arrayTex;
      this.terrainUniforms.uTileCount.value = tileCount;
    };
    img.src = url;
  }

  updateForestOverlay(efaUint8) {
    const ctx = this.forestCanvas.getContext('2d');
    const img = ctx.createImageData(128, 128);
    const d = img.data;
    for (let i = 0; i < 16384; i++) {
      const v = efaUint8[i];
      if (v !== 255 && v !== 0) {
        d[i * 4 + 0] = 76;
        d[i * 4 + 1] = 175;
        d[i * 4 + 2] = 80;
        d[i * 4 + 3] = 145;
      } else {
        d[i * 4 + 3] = 0;
      }
    }
    ctx.putImageData(img, 0, 0);
    this.forestTex.needsUpdate = true;
  }

  loadModels(models) {
    for (const g of this.modelGeometries.values()) g.dispose();
    this.modelGeometries.clear();

    for (const m of models) {
      let geo;
      if (m.positions && m.positions.length >= 9) {
        const arr = new Float32Array(m.positions.length);
        for (let i = 0; i < m.positions.length; i++) {
          arr[i] = m.positions[i] * SCENE_SCALE;
        }
        geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.BufferAttribute(arr, 3));
        geo.computeVertexNormals();
      } else {
        const bb = m.bbox || [-60, 0, -60, 60, 60, 60];
        const sx = Math.max(0.4, (bb[3] - bb[0]) * SCENE_SCALE);
        const sy = Math.max(0.4, (bb[4] - bb[1]) * SCENE_SCALE);
        const sz = Math.max(0.4, (bb[5] - bb[2]) * SCENE_SCALE);
        geo = new THREE.BoxGeometry(sx, sy, sz);
        geo.translate(
          ((bb[0] + bb[3]) * 0.5) * SCENE_SCALE,
          ((bb[1] + bb[4]) * 0.5) * SCENE_SCALE,
          ((bb[2] + bb[5]) * 0.5) * SCENE_SCALE
        );
      }
      this.modelGeometries.set(m.model_id, geo);
    }
  }

  rebuildObjects(objects, selectedIndex) {
    this.transform.detach();
    while (this.objectsGroup.children.length > 0) {
      const ch = this.objectsGroup.children.pop();
      ch.material.dispose();
    }
    this.objectMeshes = [];

    const fallbackGeo = new THREE.BoxGeometry(1.5, 1.5, 1.5);
    for (let i = 0; i < objects.length; i++) {
      const o = objects[i];
      const geo = this.modelGeometries.get(o.model_id) || fallbackGeo;
      const mat = new THREE.MeshStandardMaterial({
        color: colorForModel(o.model_id),
        roughness: 0.65,
        metalness: 0.1,
        side: THREE.DoubleSide,
      });
      const mesh = new THREE.Mesh(geo, mat);
      mesh.position.set(o.x * SCENE_SCALE, o.y * SCENE_SCALE, o.z * SCENE_SCALE);
      mesh.userData.objIndex = i;
      this.objectsGroup.add(mesh);
      this.objectMeshes.push(mesh);
    }
    this.setSelected(selectedIndex);
  }

  updateObjectTransform(idx, x, y, z) {
    const mesh = this.objectMeshes[idx];
    if (!mesh) return;
    mesh.position.set(x * SCENE_SCALE, y * SCENE_SCALE, z * SCENE_SCALE);
  }

  setSelected(idx) {
    if (this.selectedIndex >= 0 && this.objectMeshes[this.selectedIndex]) {
      this.objectMeshes[this.selectedIndex].material.emissive.setHex(0x000000);
    }
    this.selectedIndex = idx;
    this.clearAxisLock();
    if (idx >= 0 && this.objectMeshes[idx]) {
      const mesh = this.objectMeshes[idx];
      mesh.material.emissive.setHex(0x663000); // Blender orange active glow
      this.transform.attach(mesh);
      this.transform.visible = this.activeTool === 'select';
      this.transform.enabled = this.activeTool === 'select';
    } else {
      this.transform.detach();
    }
  }

  focusWorldPoint(wx, wy, wz, dist = 120) {
    const tx = wx * SCENE_SCALE;
    const ty = wy * SCENE_SCALE;
    const tz = wz * SCENE_SCALE;
    this.orbit.target.set(tx, ty, tz);
    if (this.isOrtho) {
      const dir = new THREE.Vector3().subVectors(this.orthoCamera.position, this.orbit.target);
      if (dir.lengthSq() < 1e-4) dir.set(0, 1, 0.01);
      dir.normalize();
      this.orthoCamera.position.copy(this.orbit.target).addScaledVector(dir, 3000);
      this.orthoCamera.zoom = Math.max(0.5, this.orthoCamera.top / (dist * 0.7));
      this.orthoCamera.updateProjectionMatrix();
    } else {
      this.perspCamera.position.set(tx + dist * 0.7, ty + dist * 0.65, tz + dist * 0.7);
    }
    this.orbit.update();
  }

  getPivotWorldPos() {
    return {
      x: this.orbit.target.x / SCENE_SCALE,
      y: this.orbit.target.y / SCENE_SCALE,
      z: this.orbit.target.z / SCENE_SCALE,
    };
  }

  _drawNavGizmo() {
    if (!this.navCtx || !this.navCanvas) return;
    const ctx = this.navCtx;
    const w = this.navCanvas.width;
    const h = this.navCanvas.height;
    const cx = w * 0.5;
    const cy = h * 0.5;
    const radius = w * 0.34;

    ctx.clearRect(0, 0, w, h);

    const invQ = this.camera.quaternion.clone().invert();
    const pts = GIZMO_AXES.map((a) => {
      const v = a.vec.clone().applyQuaternion(invQ);
      return {
        ...a,
        x: cx + v.x * radius,
        y: cy - v.y * radius,
        z: v.z,
        r: a.positive ? 10 : 7,
      };
    });
    pts.sort((a, b) => a.z - b.z);
    this.navProjected = pts;

    // Draw axis stems for positive axes
    for (const p of pts) {
      if (p.positive) {
        ctx.strokeStyle = p.color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.lineTo(p.x, p.y);
        ctx.stroke();
      }
    }

    // Draw axis balls
    ctx.font = 'bold 10px Inter, Segoe UI, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (const p of pts) {
      ctx.beginPath();
      ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
      ctx.fillStyle = p.color;
      ctx.fill();
      if (!p.positive) {
        ctx.strokeStyle = 'rgba(255,255,255,0.35)';
        ctx.lineWidth = 1;
        ctx.stroke();
      } else {
        ctx.fillStyle = '#111';
        ctx.fillText(p.label, p.x, p.y + 0.5);
      }
    }
  }

  _animate() {
    requestAnimationFrame(() => this._animate());
    const now = performance.now();
    const dt = Math.min(0.05, (now - this._lastTime) / 1000);
    this._lastTime = now;

    // Walk/Fly movement when Fly mode is active or Alt+RMB held
    if ((this.flyMode || this.isAltRmbFly) && this.keys.size > 0) {
      const fast = this.keys.has('ShiftLeft') || this.keys.has('ShiftRight');
      const speed = (fast ? 950 : 280) * dt;
      const forward = new THREE.Vector3();
      this.camera.getWorldDirection(forward);
      if (forward.lengthSq() > 0.0001) forward.normalize();
      const right = new THREE.Vector3().crossVectors(forward, new THREE.Vector3(0, 1, 0)).normalize();
      const move = new THREE.Vector3();

      if (this.keys.has('KeyW')) move.addScaledVector(forward, speed);
      if (this.keys.has('KeyS')) move.addScaledVector(forward, -speed);
      if (this.keys.has('KeyD')) move.addScaledVector(right, speed);
      if (this.keys.has('KeyA')) move.addScaledVector(right, -speed);
      if (this.keys.has('KeyE')) move.y += speed;
      if (this.keys.has('KeyQ')) move.y -= speed;

      if (move.lengthSq() > 0) {
        this.camera.position.add(move);
        this.orbit.target.add(move);
      }
    }

    if (!this.flyMode && !this.isAltRmbFly) {
      this.orbit.update();
    }
    this.renderer.render(this.scene, this.camera);
    // Redraw 2D nav gizmo only when camera rotation actually changed
    if (this._gizmoDirty || this._lastGizmoQuat.angleTo(this.camera.quaternion) > 0.002) {
      this._gizmoDirty = false;
      this._lastGizmoQuat.copy(this.camera.quaternion);
      this._drawNavGizmo();
    }
  }
}
