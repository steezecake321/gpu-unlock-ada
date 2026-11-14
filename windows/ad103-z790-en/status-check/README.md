# RTX 4070 Ti SUPER Windows status check

This is a read-only Windows observer. It reports **stock-like / unlocked-like
performance** only; it never claims to read SS0/SS1 or any live hardware
unlock bit. It does not boot EFI, modify firmware, set GPU clocks/power, or
write GPU registers. CUDA allocations and arithmetic kernels are used only
for the numerical check and timing workload.

## Evidence found before implementation

- The current EFI application writes a USB log. Its `TRANSACTION OK` line is
  written before `GuBootWindows()` starts Windows, and the code intentionally
  says that line does not prove Windows started. There is no EFI handoff
  variable writer and no Windows startup observer in the Z790 tree. Therefore
  **same-boot receipt status is unavailable**; the checker records it as
  unavailable rather than treating an old USB line as current-boot proof.
- `nvidia-smi` reports device identity, driver, VBIOS, telemetry, and CUDA
  support level. It exposes no supported SS0/SS1 selector read. The archived
  register captures are pre-driver EFI probe outputs, not a live Windows API.
- The external GA102 `work/windows-status` path from the request was not
  present in this environment. Local evidence is under
  `repo/docs/evidence/ada2705-windows-soak/`.

## Classifier ratio-v2 (2026-11-14) — driver-agnostic, supersedes the section below

`check_status.py` now prints a verdict of `LOCKED`, `UNLOCKED` or
`INCONCLUSIVE` from same-run ratios only. Driver, CUDA runtime, VBIOS and
benchmark hashes are recorded as provenance; a mismatch is a *warning*, never a
reason for `INCONCLUSIVE`, so driver updates do not break it. The bands in
`calibration.json` (schema 2) are backed by 14 saved sessions across drivers
610.88, 621.14 and 621.24 and five boots: locked ~0.50 / ~1.00, unlocked
~1.00 / ~2.00 (FP32-accumulate / FP8 over FP16-accumulate). `INCONCLUSIVE`
remains only for real measurement problems: failed numerical validation, wrong
GPU, FP16 control under 120 TFLOP/s, FFMA control ratio outside 0.20-0.30,
timing spread over 20% after 3 attempts, or ratios matching neither state.
Regression test: `python tests/test_ratio_classifier.py`. Default executables
are `build/*.exe`; default idle ceiling is 40% (desktop alone reads 20-35%);
an unreachable ComfyUI queue is treated as idle. The previous classifier and
calibration are in `D:\AI\backups_old\2026-11-14_ad103-status-check-pre-ratio-v2`.

## Local calibration and limit (historical, pre-ratio-v2)

The saved 5-minute stock and unlocked sessions use this exact card, VBIOS,
driver 621.14, CUDA 13.5, and the repository's `mma_bench.cu`. Their median
per-invocation-best rates were:

| Mode | Stock | Unlocked |
|---|---:|---:|
| FP16 input / FP32 accumulate | 88.8 TFLOP/s | 195.7 TFLOP/s |
| BF16 input / FP32 accumulate | 90.4 TFLOP/s | 195.4 TFLOP/s |
| FP8 E4M3 | 182.1 TFLOP/s | 392.2 TFLOP/s |
| FP16 input / FP16 accumulate control | 182.6 TFLOP/s | 195.4 TFLOP/s |
| scalar FP32 FFMA control | 45.2 TFLOP/s | 47.2 TFLOP/s |

The three classifier ratios normalize FP16/FP32, BF16/FP32, and FP8 to the
same-run FP16/FP16 control. Current bands are deliberately disjoint. The
evidence has one five-minute session per state, however, and the saved
benchmark hash is abbreviated (`cc534b4f...8069d`). `calibration.json` therefore
keeps classification **disabled** until two independent sessions per state
and a complete executable/source fingerprint are recorded. Until then the
result is always `inconclusive`, even if the live numbers look similar.

The workload has no cuBLAS dependency; `validate_gemm.exe` separately uses
cuBLAS to compare FP16/BF16 outputs against CPU reference values, and
`validate_fp8.exe` checks all 512 output values of a small E4M3 MMA against
its exact expected result. The report captures GPU/driver/VBIOS, CUDA Toolkit
and runtime/library presence, source and executable hashes, repeat timings,
temperature/power, and Windows boot time.

## Build and run

From this directory in PowerShell:

```powershell
./Build.ps1
python ./check_status.py --bench ./build/mma_bench.exe --validator ./build/validate_gemm.exe --fp8-validator ./build/validate_fp8.exe
```

The checker requires the local ComfyUI queue endpoint to respond and be empty,
GPU utilization at or below 10%, and at least 1800 MiB free VRAM. The default
utilization ceiling can be raised for a specific run with
`--max-gpu-utilization PERCENT`; the chosen ceiling is recorded in the report.
Use an override only after confirming that GPU activity is background/idle
activity and the queue is empty. If a precondition cannot be verified, it saves
an `inconclusive` report and does not start CUDA work. Default reports go under
`%LOCALAPPDATA%\AD103Status\reports`. An alternate queue URL can be supplied
with `--queue-url`.

No login task is registered by this package. Automatic checks should wait
until the EFI path has a reviewed same-boot receipt writer and Windows
observer; otherwise every automatic report would correctly say receipt
unavailable. The existing Secure Boot policy, signing keys, local profiles,
and boot gates are untouched.
