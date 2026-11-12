# Reproducing the measurements

Historical results and limits: [GA102 evidence](../../../docs/evidence/ga102-z790-windows/README.md).

## Native CUDA

`gemm_bench.cu` is the exact source used for the measured native pair. Build with CUDA Toolkit 13.5.37, its cuBLAS, VS 2022 x64 C++ tools and Windows SDK; the original executable SHA256 is recorded with the evidence. `Build.ps1` discovers the toolkit and MSVC. `Run.ps1 -State stock` or `-State unlocked` labels a run, checks a valid matrix size, records a compact environment and preserves existing output. It does not detect or change the actual hardware state.

Run one state per independently chosen cold boot, with other GPU workloads idle and comparable temperatures. Keep the source, executable, toolkit, driver, size and power settings identical. Default N=4096. No clocks, fans or power limits are changed by the benchmark. The public scripts never reboot or run a firmware application.

Each mode uses dense, non-sparse A*B with beta=0. Five warm-up operations precede calibration; each of five timed batches targets at least 500 ms, capped at 1,000 operations. CUDA events exclude allocations, host transfers and CPU checking. Reported throughput is 2*N^3/time (FMA counts as two operations), using the median per-operation time. cuBLAS selects the actual kernels; no hardware counters proved their instruction mix. TF32 is explicitly permitted only in its own mode; strict FP32 uses pedantic compute.

The deterministic inputs are multiples of 1/16, exactly representable across the tested input types. Sixty-four output positions per mode are checked against double-precision CPU calculations. Zero sampled error is a fixture sanity check, not exhaustive correctness or arbitrary-input precision qualification. FP8/FP4 are not tested. Native results are throughput, not a blanket application speedup.

```powershell
.\Build.ps1
.\Run.ps1 -State stock
# In a separate independently prepared unlocked boot:
.\Run.ps1 -State unlocked
```

## ComfyUI workloads

`comfy/` preserves the five-workload kit used for the comparison and its start image. It contains no models. A running ComfyUI with the required nodes and exact model names is needed. `preflight.py` checks readiness without submitting generation work. Do not update models, nodes or versions between states. Capture stock and unlocked under distinct labels; start ComfyUI fresh for each state and keep its queue otherwise idle.

```powershell
cd comfy
python preflight.py
python run_bench_set.py default
# After a separately prepared unlocked boot and fresh ComfyUI start:
python run_bench_set.py unlocked
python compare_all.py default unlocked
python diff_outputs.py YOUR_OUTPUT_DIRECTORY wan_default wan_unlocked
```

Repeat output comparison for each workload, checking expected file counts and RGB dimensions. The historical comparison used strict counts (49 frames per Wan/LTX run, 56 per MiniMax run, one image per FLUX/Qwen run). The included original diff script zips sorted file lists and is insufficient by itself to detect unequal counts; use `compare_pixels.py` for strict verification.

Model selection, seeds, dimensions, step counts, warm-ups and run counts must remain unchanged. The original runner captures local paths/hostnames in raw environment files: keep these under ignored results/ and sanitize before sharing. The `cached` field indicates a cache event, not that the whole generation was cached; inspect its actual node list. Two-second polling affects short wall-clock measurements. Historical application results were not rerun after CUDA installation.
