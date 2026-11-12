#!/usr/bin/env python3
"""Compile and run the GA102 selector-guard host regression tests.

The test uses mocked EFI_PCI_IO_PROTOCOL callbacks only. It does not open,
map, write, or otherwise access a physical GPU.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", default="gcc", help="GCC executable (default: gcc)")
    args = parser.parse_args()
    compiler = shutil.which(args.gcc) or args.gcc

    # The original failure was a missing call-site sequence, so test the
    # integration point as well as the guard's behavior in the C harness.
    vendor_source = (root / "vendor/unlock_610.c").read_text(encoding="utf-8")
    selector_body = vendor_source.split(
        "static int unlock_transaction_selector(struct unlock_transaction_work *work)", 1
    )[1].split("/* Reset SEC2", 1)[0]
    if "if (!work->payload_verified || check_entry_selectors(work))" not in selector_body:
        raise SystemExit("selector pre-write checks missing")
    required = [
        'write_selector(work, "SS0"',
        "GuSelectorPhase(GU_SELECTOR_PHASE_SS0_WRITTEN)",
        "GuSelectorPhase(GU_SELECTOR_PHASE_SS1_WRITE)",
        'write_selector(work, "SS1"',
        "GuSelectorPhase(GU_SELECTOR_PHASE_TARGET)",
    ]
    positions = [selector_body.find(token) for token in required]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise SystemExit("selector source wiring check failed: phase sequence missing or out of order")
    probe_body = vendor_source.split("static void probe_blackout(", 1)[1].split(
        "/* Write the unlock value", 1
    )[0]
    if "GuDiagProbe(&chip, &chip_status, &pciid, &pci_status)" not in probe_body:
        raise SystemExit("selector source wiring check failed: blackout probe is not using GuDiagProbe")
    print("selector source wiring: passed")

    (root / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ga102-selector-test-", dir=root / "build") as tmp:
        binary = Path(tmp) / "test-selector-guard.exe"
        command = [
            compiler,
            "-std=c11",
            "-O0",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-Wno-misleading-indentation",
            "-D__KERNEL__",
            "-DGU_UEFI_PORT",
            "-DGU_AD104_TRANSACTION",
            "-DGU_ENTRY_LATCHED_SS1_FULL",
            "-I.",
            "-Icompat",
            "-Ivendor/edk2/Include",
            "-Ivendor/edk2/Include/X64",
            "-fshort-wchar",
            "-ffunction-sections",
            "-fdata-sections",
            "tests/test_selector_guard.c",
            "port.c",
            "-Wl,--gc-sections",
            "-o",
            str(binary),
        ]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(binary)], cwd=root, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
