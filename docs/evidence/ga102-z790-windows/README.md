# RTX 3090 Ti / Z790 Windows evidence — 2026-11-11

Owner-contributed, single-machine evidence from steezecake321. The production transaction completed in RUN0020 and the owner returned to Windows. Source and instrumented state support successful selector application, one FLR, validated containment and complete DMA release. The public excerpts omit handles, live DMA addresses and raw machine capture. No private keys, certificates, ESP GUIDs, PROMs or firmware binaries are included.

## Native matched pair

Same SHA256-verified executable, CUDA 13.5.37 / cuBLAS reported version 131000, NVIDIA 621.14, 450 W power limit, sm_86 with 84 SMs. Runtime and driver API version both 13050. Unlocked was measured first; stock followed an owner-reported full shutdown and direct Windows boot. The native benchmark does not independently read speed-selector registers.

| Precision | Stock TFLOP/s | Unlocked TFLOP/s | Speedup |
|---|---:|---:|---:|
| FP64 pedantic | 0.565949 | 0.583216 | +3.1% |
| FP32 pedantic | 28.463555 | 29.290279 | +2.9% |
| TF32 enabled | 36.537233 | 70.729537 | +93.6% |
| FP16 / FP32 accumulation | 70.428560 | 131.693853 | +87.0% |
| BF16 / FP32 accumulation | 71.506373 | 136.603626 | +91.0% |

Dense 4096³ GEMM; medians of five timed batches. All 64 CPU-reference spot-checks per mode passed with zero sampled error in both states. Raw trials: cuda-native-stock.csv and cuda-native-unlocked.csv. Executable SHA256: `bffa456ebe56a43ce97459fbea20c7f6b0fd637fa1e3bc73d631b8a803d81fab`. These gains are about 1.87–1.94x in the tensor-math modes, not a doubling of scalar CUDA performance.

## Application results

RTX 3090 Ti 24 GiB, Core i5-13600K, approximately 47.69 GiB OS-visible RAM, Windows 11 build 26200, GPU vBIOS 94.02.a0.00.5e, driver 621.14, 450 W. ComfyUI 0.45.2 / PyTorch 2.13.0+cu130. Recorded version/device/power/launch settings matched; no model or node substitutions were made. Model-content hashes were not captured with the historical stock baseline. The PCIe environment snapshot was idle Gen 1 x8 in both states; loaded link speed was not established.

| Workload | Stock runs s | Unlocked runs s | Median stock → unlocked s | Speedup |
|---|---|---|---|---:|
| Wan | 71.2, 88.4, 70.5 | 55.6, 74.4, 52.7 | 71.2 → 55.6 | +28.1% |
| LTX | 20.1, 22.1, 20.1 | 20.1, 20.1, 20.1 | 20.1 → 20.1 | 0.0% |
| MiniMax | 32.2, 32.2, 32.2 | 28.2, 28.2, 32.2 | 32.2 → 28.2 | +14.2% |
| FLUX | 22.2, 22.2, 22.1, 22.1, 22.2 | 16.1, 16.1, 16.1, 16.1, 16.1 | 22.2 → 16.1 | +37.9% |
| Qwen | 12.1, 12.1, 12.1, 12.1, 12.1 | 10.1, 8.3, 8.1, 8.1, 8.4 | 12.1 → 8.3 | +45.8% |

Speedup = stock median / unlocked median − 1. All workloads succeeded. All **472** paired decoded RGB outputs matched exactly: 147 Wan + 147 LTX + 168 MiniMax + 5 FLUX + 5 Qwen. Raw generated images are not distributed; per-run counts/differences are in pixel-comparison.json. Output equality is observed for these seeds and settings only.

## Variability and interpretation

- Wan's slow unlocked run 2 reused no cached setup nodes; runs 1 and 3 reused nodes 1–12. Sampling nodes executed each time. Recomputed setup is a strong explanation for its extra time; the eviction trigger is unknown. The historical stock run 2 was also slow, but its detailed cache history is unavailable.
- The original `cached=true` records the presence of an execution_cached event even for an empty node list. It does not establish that generation was skipped. execution-cache-details.json preserves the actual unlocked lists.
- MiniMax run 3 reused the same setup-node list and remains unexplained. Outliers greater than 5% from their state median: stock Wan 2; unlocked Wan 2/3; stock LTX 2; unlocked MiniMax 3; unlocked Qwen 1.
- The original wall-clock harness polls every two seconds. Server medians give useful corroboration: Wan 69.1 → 52.5 s; LTX 19.1 → 18.8; MiniMax 31.3 → 27.1; FLUX 21.6 → 14.3; Qwen 11.5 → 8.0. In particular Qwen's wall-time gain is affected by polling quantization.
- Application stock was measured hours before unlocked, with fresh ComfyUI for each state. Native ordering was the reverse (unlocked then stock). There is no repeated alternating cold-boot series; clocks/temperatures were not locked. Timing batches within a state are not independent boot replications.
- benchmark-summary.json includes telemetry summaries for samples with GPU utilization above 90%; gpu_*.csv are the raw samples. Sampling spans warm-up and timed work, not isolated kernels. The unlocked GPU commonly used more power at lower clocks; no settings were changed.

## Limits and provenance

This is one successful complete unlock boot followed by short workload checks. Long-term stability, sleep/hibernate, warm-reset persistence, other firmware/driver versions and other physical boards remain unqualified. Numerical spot-checks and one set of identical application images do not prove universal correctness.

The historically booted signed EFI SHA256 was `5c0a4633d3ccec65866d9c1ff0e5a3e0f0ddd89ebd692f17f4e93c6db8686ab5`. It is intentionally not distributed. Its signature was verified with the owner's local certificate, which matched the .crt on the test USB. Application policy nevertheless required Secure Boot off. Public builds use private local configuration and must not be assumed byte-identical to this historical artifact.

Public validation records describe source/mock/build checks only, not an additional physical run. See the source [README](../../../windows/ga102-rogstrix-z790/README.md), [development history](../../../windows/ga102-rogstrix-z790/DEVELOPMENT.md), [benchmark protocol](../../../windows/ga102-rogstrix-z790/benchmark/README.md) and publication-manifest.json for exact public file hashes and attribution.
