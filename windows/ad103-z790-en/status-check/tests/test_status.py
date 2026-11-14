import copy
import importlib.util
import json
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("check_status", HERE / "check_status.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class StatusTests(unittest.TestCase):
    def test_parse_and_inconclusive_pending_calibration(self):
        sample = """mode       best TF/s   all runs
f16/f16        195.4 194.0/195.4/194.8/195.1/194.2
f16/f32        195.7 194.1/195.7/194.8/195.2/194.4
bf16/f32       195.4 194.0/195.4/194.8/195.1/194.2
fp8/e4m3       392.2 390.1/392.2/391.4/391.8/390.8
ffma            47.2 46.9/47.2/47.0/47.1/46.8
"""
        modes = module.parse_bench(sample)
        self.assertAlmostEqual(module.ratios(modes)["fp16_fp32_over_fp16_fp16"], 1.0015, places=3)
        calibration = json.loads((HERE / "calibration.json").read_text())
        state, reasons = module.classify(modes, calibration, "a" * 64, "b" * 64,
            {"device_id": "0x270510de", "vbios": "95.03.45.80.07", "driver": "621.14", "cuda_toolkit": "13.5"})
        self.assertEqual(state, "inconclusive")
        self.assertTrue(any("not yet qualified" in item for item in reasons))

    def test_requires_all_modes(self):
        with self.assertRaisesRegex(ValueError, "missing modes"):
            module.parse_bench("f16/f16 100 99/100/98\n")

    def test_rejects_unstable_repetitions(self):
        modes = {name: {"best": 100.0, "samples": [80, 100, 120], "median": 100,
                        "range_fraction": 0.40} for name in module.MODES}
        calibration = json.loads((HERE / "calibration.json").read_text())
        calibration["status"] = "qualified"
        calibration["benchmark_sha256"] = "a" * 64
        calibration["source_sha256"] = "b" * 64
        calibration["sessions_available"] = {"stock": 2, "unlocked": 2}
        calibration["fingerprint"]["cuda_runtime_and_library_sha256"] = {"cudart64_13.dll": "c" * 64}
        state, reasons = module.classify(modes, calibration, "a" * 64, "b" * 64,
            {"device_id": "0x270510de", "vbios": "95.03.45.80.07", "driver": "621.14", "cuda_toolkit": "13.5",
             "cuda_runtime_and_library_sha256": {"cudart64_13.dll": "c" * 64}})
        self.assertEqual(state, "inconclusive")
        self.assertTrue(any("unstable" in item for item in reasons))

    def test_unique_unlocked_profile_requires_exact_fingerprint(self):
        values = {"f16/f16": 100.0, "f16/f32": 100.0, "bf16/f32": 100.0,
                  "fp8/e4m3": 200.0, "ffma": 25.0}
        modes = {name: {"best": value, "samples": [value * 0.995, value, value * 1.005],
                        "median": value, "range_fraction": 0.01}
                 for name, value in values.items()}
        calibration = json.loads((HERE / "calibration.json").read_text())
        calibration["status"] = "qualified"
        calibration["benchmark_sha256"] = "a" * 64
        calibration["source_sha256"] = "b" * 64
        calibration["sessions_available"] = {"stock": 2, "unlocked": 2}
        dlls = {"cudart64_13.dll": "c" * 64}
        calibration["fingerprint"]["cuda_runtime_and_library_sha256"] = dlls
        state, reasons = module.classify(modes, calibration, "a" * 64, "b" * 64,
            {"device_id": "0x270510de", "vbios": "95.03.45.80.07", "driver": "621.14",
             "cuda_toolkit": "13.5", "cuda_runtime_and_library_sha256": dlls})
        self.assertEqual(state, "unlocked-like performance")
        self.assertEqual(reasons, [])


if __name__ == "__main__":
    unittest.main()
