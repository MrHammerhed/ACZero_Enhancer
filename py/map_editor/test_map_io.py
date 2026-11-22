#!/usr/bin/env python3
"""Unit & round-trip verification tests for py/map_editor/map_io.py."""

import base64
import json
import sys
import tempfile
import threading
import unittest
import urllib.request
from pathlib import Path

ROOT_PY = Path(__file__).resolve().parent.parent
if str(ROOT_PY) not in sys.path:
    sys.path.insert(0, str(ROOT_PY))

from map_editor import map_io
from map_editor.server import run_server

GAME_DIR = Path(r"e:\Games\Ace Combat Zero The Belkan War")
MAPS_DIR = GAME_DIR / "extracted" / "a" / "data" / "map"
HD_TERRAIN_DIR = GAME_DIR / "mods" / "textures" / "terrain"


class TestMapIO(unittest.TestCase):
    def _verify_map_roundtrip(self, map_name: str):
        map_dir = MAPS_DIR / map_name
        if not map_dir.is_dir():
            self.skipTest(f"{map_dir} not found")

        orig_pdl = (map_dir / f"{map_name}.pdl").read_bytes()
        orig_pml = (map_dir / f"{map_name}.pml").read_bytes()
        orig_pmi = (map_dir / f"{map_name}.pmi").read_bytes()
        orig_efa = (map_dir / f"{map_name}.efa").read_bytes()

        objs = map_io.parse_pdl(orig_pdl)
        models = map_io.load_pmmdl_catalog(map_dir, map_name)
        pdl_out, pml_out, pmi_out, warnings = map_io.serialize_placed_objects(objs, models=models)
        self.assertEqual(warnings, [], f"{map_name} produced warnings: {warnings}")

        # 1. PDL must be byte-exact
        self.assertEqual(pdl_out, orig_pdl, f"{map_name}.pdl round-trip mismatch")

        # 2. PMI + PML spatial lookup: every object placed in its center tile must be present
        new_pml_list = map_io.parse_pml(pml_out)
        new_pmi_grid = map_io.parse_pmi(pmi_out)
        for idx, obj in enumerate(objs):
            gx, gz = map_io.world_to_tile(obj["x"], obj["z"])
            pml_idx = int(new_pmi_grid[gz, gx])
            self.assertGreater(pml_idx, 0, f"{map_name}: obj[{idx}] missing from center tile ({gx},{gz})")
            self.assertIn(idx, new_pml_list[pml_idx], f"{map_name}: obj[{idx}] not in PML[{pml_idx}]")

        # 3. EFA must be byte-exact
        efa_grid = map_io.parse_efa(orig_efa)
        self.assertEqual(map_io.serialize_efa(efa_grid), orig_efa, f"{map_name}.efa round-trip mismatch")

    def test_roundtrip_m01a(self):
        self._verify_map_roundtrip("m01a")

    def test_roundtrip_m02a(self):
        self._verify_map_roundtrip("m02a")

    def test_roundtrip_m06a(self):
        self._verify_map_roundtrip("m06a")

    def test_m02a_models_and_heightmap(self):
        map_dir = MAPS_DIR / "m02a"
        if not map_dir.is_dir():
            self.skipTest("m02a not found")

        hm = map_io.load_heightmap(map_dir, "m02a")
        self.assertIsNotNone(hm)
        self.assertEqual(hm.grid_raw.shape, (513, 513))

        objs = map_io.parse_pdl((map_dir / "m02a.pdl").read_bytes())
        scale = map_io.calibrate_height_scale(hm, objs)
        self.assertGreater(scale, 50.0)
        self.assertLess(scale, 120.0)

        models = map_io.load_pmmdl_catalog(map_dir, "m02a")
        self.assertEqual(len(models), 44)
        total_tris = sum(m["tri_count"] for m in models)
        self.assertGreater(total_tris, 200, "Expected decoded ACM triangles from pmmdlm02a.bin")

    def test_terrain_overview_render(self):
        map_dir = MAPS_DIR / "m02a"
        if not map_dir.is_dir():
            self.skipTest("m02a not found")
        img = map_io.render_terrain_overview(map_dir, "m02a", HD_TERRAIN_DIR, use_hd=True, out_size=1024)
        self.assertIsNotNone(img)
        self.assertEqual(img.size, (1024, 1024))

    def test_http_server_api_and_save(self):
        if not (MAPS_DIR / "m02a").is_dir():
            self.skipTest("m02a not found")

        with tempfile.TemporaryDirectory() as tmp:
            ovr_root = Path(tmp) / "maps"
            srv = run_server(MAPS_DIR, HD_TERRAIN_DIR, ovr_root, host="127.0.0.1", port=0)
            port = srv.server_address[1]
            t = threading.Thread(target=srv.serve_forever, daemon=True)
            t.start()
            try:
                base = f"http://127.0.0.1:{port}"
                with urllib.request.urlopen(f"{base}/api/maps") as r:
                    maps_data = json.loads(r.read().decode("utf-8"))
                self.assertTrue(any(m["name"] == "m02a" for m in maps_data["maps"]))

                with urllib.request.urlopen(f"{base}/api/map/m02a/meta") as r:
                    meta = json.loads(r.read().decode("utf-8"))
                self.assertEqual(len(meta["objects"]), 105)
                self.assertEqual(len(meta["models"]), 44)

                with urllib.request.urlopen(f"{base}/api/map/m02a/heightmap.bin?step=2") as r:
                    hm_bytes = r.read()
                    self.assertEqual(r.headers["X-Grid-Dim"], "257")
                self.assertEqual(len(hm_bytes), 257 * 257 * 4)

                with urllib.request.urlopen(f"{base}/api/map/m02a/efa.bin") as r:
                    efa_bytes = r.read()
                self.assertEqual(len(efa_bytes), 16384)

                # Test non-destructive save to temporary overrides dir
                new_objs = meta["objects"] + [{"x": 15000.0, "y": 500.0, "z": -230000.0, "model_id": 0}]
                payload = json.dumps({
                    "objects": new_objs,
                    "efa_b64": base64.b64encode(efa_bytes).decode("ascii"),
                }).encode("utf-8")
                req = urllib.request.Request(
                    f"{base}/api/map/m02a/save",
                    data=payload,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(req) as r:
                    save_res = json.loads(r.read().decode("utf-8"))
                self.assertEqual(save_res["object_count"], 106)
                for ext in ("pdl", "pml", "pmi", "efa"):
                    self.assertTrue((ovr_root / "m02a" / f"m02a.{ext}").is_file())
            finally:
                srv.shutdown()
                srv.server_close()


if __name__ == "__main__":
    unittest.main()
