#!/usr/bin/env python3
"""Read-only AD103 tensor-unlock performance observer (classifier ratio-v2).

Runs a fixed tensor-core microbenchmark and classifies the card as LOCKED or
UNLOCKED from same-run throughput ratios. It never reads/writes GPU registers.

Driver-agnostic by design: every tensor rate is divided by the same-run
FP16-input/FP16-accumulate control, so driver, clock and CUDA-runtime changes
cancel out. Driver/VBIOS/CUDA/benchmark hashes are recorded as provenance and
mismatches become warnings, never a reason for "inconclusive".
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CALIBRATION = ROOT / "calibration.json"
MODES = ("f16/f32", "bf16/f32", "fp8/e4m3", "f16/f16", "ffma")
STATE_KEYS = ("fp16_fp32_over_fp16_fp16", "bf16_fp32_over_fp16_fp16", "fp8_over_fp16_fp16")
ROW = re.compile(r"^\s*(f16/f32|bf16/f32|fp8/e4m3|f16/f16|ffma)\s+([0-9.]+)\s+([0-9./]+)\s*$")
CLAIM = ("tensor-core throttle signature from same-run throughput ratios; "
         "performance-based, not a live SS0/SS1 register read")


def parse_bench(text: str) -> dict:
    parsed = {}
    for line in text.splitlines():
        match = ROW.match(line)
        if not match:
            continue
        name, best, samples = match.groups()
        values = [float(v) for v in samples.split("/")]
        if not values or any(v <= 0 for v in values):
            raise ValueError(f"invalid timing series for {name}")
        parsed[name] = {"best": float(best), "samples": values,
                        "median": statistics.median(values),
                        "range_fraction": (max(values) - min(values)) / statistics.median(values)}
    missing = sorted(set(MODES) - set(parsed))
    if missing:
        raise ValueError("benchmark output missing modes: " + ", ".join(missing))
    return parsed


def ratios(modes: dict) -> dict:
    ref = modes["f16/f16"]["best"]
    return {"fp16_fp32_over_fp16_fp16": modes["f16/f32"]["best"] / ref,
            "bf16_fp32_over_fp16_fp16": modes["bf16/f32"]["best"] / ref,
            "fp8_over_fp16_fp16": modes["fp8/e4m3"]["best"] / ref,
            "ffma_over_fp16_fp16": modes["ffma"]["best"] / ref}


def unstable_modes(modes: dict, calibration: dict) -> list[str]:
    limit = calibration["gates"]["max_range_fraction_per_mode"]
    return [name for name in MODES if modes[name]["range_fraction"] > limit]


def classify(modes: dict, calibration: dict, provenance: dict | None = None) -> dict:
    """Pure function: benchmark modes -> verdict. Provenance only yields warnings."""
    provenance = provenance or {}
    gates = calibration["gates"]
    r = ratios(modes)
    reasons: list[str] = []
    warnings: list[str] = []

    device = str(provenance.get("device_id", "")).lower()
    if device and device != calibration["device_id"]:
        reasons.append(f"device {device} is not the calibrated AD103 {calibration['device_id']}")
    vbios = provenance.get("vbios")
    if vbios and vbios != calibration.get("expected_vbios"):
        warnings.append(f"VBIOS {vbios} differs from {calibration.get('expected_vbios')} (recorded only)")
    exe = str(provenance.get("benchmark_executable_sha256", "")).lower()
    if exe and exe not in calibration.get("known_benchmark_sha256", {}):
        warnings.append("benchmark executable hash is not in the known list (recorded only)")

    unstable = unstable_modes(modes, calibration)
    if unstable:
        reasons.append("repeat timings unstable (>{:.0%} spread) for: {}".format(
            gates["max_range_fraction_per_mode"], ", ".join(unstable)))
    control = modes["f16/f16"]["best"]
    if control < gates["min_fp16_fp16_control_TFLOPS"]:
        reasons.append(f"FP16/FP16 control only {control:.1f} TFLOP/s; GPU busy, throttled or shared")
    lo, hi = gates["control_ratio_band_ffma_over_fp16_fp16"]
    if not lo <= r["ffma_over_fp16_fp16"] <= hi:
        reasons.append(f"FFMA/FP16 control ratio {r['ffma_over_fp16_fp16']:.3f} outside [{lo}, {hi}]")

    matches = [state for state, bands in calibration["states"].items()
               if all(bands[k][0] <= r[k] <= bands[k][1] for k in STATE_KEYS)]
    if not reasons and len(matches) != 1:
        reasons.append("ratios do not uniquely match LOCKED or UNLOCKED: " +
                       ", ".join(f"{k}={r[k]:.3f}" for k in STATE_KEYS))
    verdict = matches[0].upper() if not reasons else "INCONCLUSIVE"
    return {"verdict": verdict, "status": "conclusive" if verdict != "INCONCLUSIVE" else "inconclusive",
            "ratios": r, "reasons": reasons, "warnings": warnings,
            "classifier": calibration.get("classifier", "ratio-v2"), "claim": CLAIM}


def run(command: list[str], timeout: int = 120) -> str:
    p = subprocess.run(command, check=False, capture_output=True, text=True, timeout=timeout)
    if p.returncode:
        raise RuntimeError(f"command failed ({p.returncode}): {' '.join(command)}\n{p.stderr.strip()}")
    return p.stdout


def query_queue(url: str, timeout: float = 2.0) -> dict:
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            queue = json.loads(response.read().decode("utf-8"))
    except OSError:
        return {"url": url, "reachable": False}  # ComfyUI not running: nothing to compete with
    running, pending = queue.get("queue_running"), queue.get("queue_pending")
    if not isinstance(running, list) or not isinstance(pending, list):
        raise ValueError("ComfyUI queue response has an unexpected shape")
    if running or pending:
        raise RuntimeError(f"ComfyUI queue is active (running={len(running)}, pending={len(pending)})")
    return {"url": url, "reachable": True, "running": 0, "pending": 0}


def boot_time_utc() -> str:
    import ctypes
    get_ticks = ctypes.windll.kernel32.GetTickCount64
    get_ticks.restype = ctypes.c_ulonglong
    stamp = dt.datetime.now(dt.timezone.utc) - dt.timedelta(milliseconds=get_ticks())
    return stamp.replace(microsecond=0).isoformat()


def find_source() -> Path | None:
    for candidate in (ROOT / "mma_bench.cu", ROOT.parent / "diagnostics" / "bench" / "mma_bench.cu",
                      ROOT.parents[2] / "diagnostics" / "bench" / "mma_bench.cu"):
        if candidate.is_file():
            return candidate
    return None


def reclassify(path: Path, calibration: dict, out: Path | None) -> int:
    report = json.loads(path.read_text(encoding="utf-8"))
    gpu, software = report.get("gpu", {}), report.get("software", {})
    if ("failures=0" not in report.get("numerical_validation", "")
            or "zero mismatches" not in report.get("fp8_numerical_validation", "")):
        result = {"verdict": "INCONCLUSIVE", "status": "inconclusive",
                  "reasons": ["saved numerical validation evidence is missing or failed"], "warnings": []}
    else:
        result = classify(report["benchmark"]["modes"], calibration,
                          {"device_id": gpu.get("device_id"), "vbios": gpu.get("vbios"),
                           "benchmark_executable_sha256": software.get("benchmark_executable_sha256")})
    report["classification"] = result
    report["status"], report["reasons"] = result["status"], result["reasons"]
    destination = out or path.with_name(path.stem + "-ratio-v2.json")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"verdict": result["verdict"], "driver": gpu.get("driver"), "report": str(destination),
                      "reasons": result["reasons"], "warnings": result["warnings"]}, indent=2))
    return 0 if result["status"] == "conclusive" else 2


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench", type=Path, default=ROOT / "build" / "mma_bench.exe")
    ap.add_argument("--validator", type=Path, default=ROOT / "build" / "validate_gemm.exe")
    ap.add_argument("--fp8-validator", type=Path, default=ROOT / "build" / "validate_fp8.exe")
    ap.add_argument("--classify-report", type=Path,
                    help="reclassify an existing JSON report without rerunning the GPU benchmark")
    ap.add_argument("--calibration", type=Path, default=CALIBRATION)
    ap.add_argument("--queue-url", default="http://127.0.0.1:8188/queue")
    ap.add_argument("--max-gpu-utilization", type=int, default=40,
                    help="refuse to benchmark above this nvidia-smi utilization percent (default 40; "
                         "desktop compositing alone reads 20-35%%)")
    ap.add_argument("--out", type=Path, help="report JSON path (default: %%LOCALAPPDATA%%\\AD103Status\\reports)")
    args = ap.parse_args()
    calibration = json.loads(args.calibration.read_text(encoding="utf-8"))
    if calibration.get("schema") != 2:
        ap.error("calibration.json is not schema 2 (ratio-v2)")
    if args.classify_report:
        return reclassify(args.classify_report, calibration, args.out)
    if not 0 <= args.max_gpu_utilization <= 100:
        ap.error("--max-gpu-utilization must be between 0 and 100")

    report = {"schema": 2, "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(), "claim": CLAIM,
              "status": "inconclusive", "verdict": "INCONCLUSIVE", "reasons": [], "warnings": []}
    try:
        if os.name != "nt":
            raise RuntimeError("this observer is Windows-only")
        for exe in (args.bench, args.validator, args.fp8_validator):
            if not exe.is_file():
                raise RuntimeError(f"missing executable {exe}")
        report["boot_time_utc"] = boot_time_utc()
        report["idle_gate"] = {**query_queue(args.queue_url), "max_gpu_utilization_percent": args.max_gpu_utilization}
        smi = run(["nvidia-smi", "--query-gpu=name,pci.device_id,vbios_version,driver_version,utilization.gpu,"
                   "memory.used,memory.total,temperature.gpu,power.draw,power.limit",
                   "--format=csv,noheader,nounits"]).strip().splitlines()
        cards = [[x.strip() for x in line.split(",")] for line in smi]
        cards = [c for c in cards if len(c) == 10 and c[1].lower() == calibration["device_id"]]
        if len(cards) != 1:
            raise RuntimeError(f"expected exactly one {calibration['device_id']} GPU, found {len(cards)}")
        c = cards[0]
        gpu = {"name": c[0], "device_id": c[1].lower(), "vbios": c[2], "driver": c[3],
               "utilization_percent": int(c[4]), "memory_used_mib": int(c[5]), "memory_total_mib": int(c[6]),
               "temperature_c": float(c[7]), "power_w": float(c[8]), "power_limit_w": c[9]}
        report["gpu"] = gpu
        if gpu["utilization_percent"] > args.max_gpu_utilization:
            raise RuntimeError(f"GPU utilization {gpu['utilization_percent']}% is above the "
                               f"{args.max_gpu_utilization}% idle threshold")
        if gpu["memory_total_mib"] - gpu["memory_used_mib"] < 1800:
            raise RuntimeError("less than 1800 MiB of free VRAM; refusing to compete with resident workloads")
        source = find_source()
        software = {"benchmark_source": str(source) if source else None,
                    "benchmark_source_sha256": hashlib.sha256(source.read_bytes()).hexdigest() if source else None,
                    "benchmark_executable_sha256": hashlib.sha256(args.bench.read_bytes()).hexdigest(),
                    "benchmark_args": ["20000", "5"]}
        try:
            nvcc = run(["nvcc", "--version"])
            m = re.search(r"release\s+(\d+\.\d+)", nvcc)
            software["cuda_toolkit"] = m.group(1) if m else "unknown"
        except (OSError, RuntimeError):
            software["cuda_toolkit"] = "nvcc not on PATH (recorded only)"
        report["software"] = software
        report["numerical_validation"] = run([str(args.validator.resolve())], timeout=90).strip()
        report["fp8_numerical_validation"] = run([str(args.fp8_validator.resolve())], timeout=30).strip()
        if "failures=0" not in report["numerical_validation"] or "zero mismatches" not in report["fp8_numerical_validation"]:
            raise RuntimeError("numerical validation failed; tensor results are not trustworthy")
        attempts = []
        for _ in range(calibration["gates"]["benchmark_attempts_if_unstable"]):
            raw = run([str(args.bench.resolve()), "20000", "5"], timeout=120)
            modes = parse_bench(raw)
            attempts.append({"modes": modes, "ratios": ratios(modes), "raw_stdout": raw})
            if not unstable_modes(modes, calibration):
                break
        report["benchmark"] = attempts[-1]
        report["benchmark_attempts"] = len(attempts)
        after = run(["nvidia-smi", "--query-gpu=temperature.gpu,power.draw,utilization.gpu,memory.used",
                     "--format=csv,noheader,nounits"]).strip().splitlines()
        report["gpu_after"] = [v.strip() for v in after[0].split(",")] if after else []
        result = classify(attempts[-1]["modes"], calibration,
                          {"device_id": gpu["device_id"], "vbios": gpu["vbios"],
                           "benchmark_executable_sha256": software["benchmark_executable_sha256"]})
        report["classification"] = result
        report.update(status=result["status"], verdict=result["verdict"],
                      reasons=result["reasons"], warnings=result["warnings"])
    except Exception as exc:  # every failure is recorded, never silently "locked"
        report["reasons"].append(str(exc))
    destination = args.out
    if destination is None:
        base = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "AD103Status" / "reports"
        destination = base / (dt.datetime.now().strftime("%Y%m%d-%H%M%S") + ".json")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"verdict": report["verdict"], "driver": report.get("gpu", {}).get("driver"),
                      "ratios": report.get("classification", {}).get("ratios"),
                      "report": str(destination), "reasons": report["reasons"],
                      "warnings": report["warnings"]}, indent=2))
    return 0 if report["status"] == "conclusive" else 2


if __name__ == "__main__":
    sys.exit(main())
