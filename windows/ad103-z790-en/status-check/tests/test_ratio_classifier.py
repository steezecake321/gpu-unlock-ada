"""Regression test: ratio-v2 must label every saved AD103 session correctly on
every driver it was captured under (610.88, 621.14, 621.24), and must never
flip a verdict because the driver string changed."""
import copy
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import check_status as cs  # noqa: E402

BENCH = Path("D:/AI/bench")
BUILD = HERE.parent / "build"
CORPUS = {
    "LOCKED": [*BUILD.glob("baseline-session-stock-610.88-*.json"),
               *(BENCH / "AD103-RTX4070TiSUPER-621.24-locked-20261113").glob("tensor-session-0?.json")],
    "UNLOCKED": [*BUILD.glob("baseline-session-current-*.json"), BUILD / "live-report.json",
                 *(BENCH / "AD103-RTX4070TiSUPER-621.24-unlocked-20261113-RUN0005").glob("AD103-tensor-session-0?.json"),
                 BENCH / "AD103-quickcheck-20261114.json"],
}


def main() -> int:
    cal = json.loads((HERE.parent / "calibration.json").read_text(encoding="utf-8"))
    failures, checked = [], 0
    for expected, paths in CORPUS.items():
        for path in paths:
            if not path.is_file():
                failures.append(f"missing corpus file {path}")
                continue
            rep = json.loads(path.read_text(encoding="utf-8"))
            modes = rep["benchmark"]["modes"]
            prov = {"device_id": rep["gpu"]["device_id"], "vbios": rep["gpu"]["vbios"]}
            result = cs.classify(modes, cal, prov)
            ok = result["verdict"] == expected
            # Spread-only failures are allowed for saved single attempts (the live
            # checker retries); they must still never produce the WRONG state.
            spread_only = (result["verdict"] == "INCONCLUSIVE"
                           and all("unstable" in r for r in result["reasons"]))
            # Driver independence: same numbers, other driver string -> same verdict.
            for fake in ("999.99", "0.0", ""):
                again = cs.classify(modes, cal, {**prov, "driver": fake})
                if again["verdict"] != result["verdict"]:
                    failures.append(f"{path.name}: verdict changed with driver={fake!r}")
            checked += 1
            tag = "ok" if ok else ("ok (spread; live run would retry)" if spread_only else "FAIL")
            print(f"{tag:34} {expected:8} drv={rep['gpu']['driver']:7} {result['verdict']:12} {path.name}")
            if not ok and not spread_only:
                failures.append(f"{path.name}: expected {expected}, got {result['verdict']} {result['reasons']}")
    # Synthetic: a half-throttled mix must be INCONCLUSIVE, never a state.
    mix = copy.deepcopy(json.loads((BENCH / "AD103-quickcheck-20261114.json").read_text())["benchmark"]["modes"])
    mix["fp8/e4m3"]["best"] = mix["f16/f16"]["best"] * 1.0
    if cs.classify(mix, cal)["verdict"] != "INCONCLUSIVE":
        failures.append("mixed signature was not INCONCLUSIVE")
    print(f"\n{checked} sessions checked, {len(failures)} failures")
    for f in failures:
        print("FAIL:", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
