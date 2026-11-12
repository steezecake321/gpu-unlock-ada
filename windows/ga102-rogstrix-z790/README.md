# RTX 3090 Ti / ASUS Z790 — experimental Windows UEFI port

A source-only, manual USB port for **one RTX 3090 Ti (GA102, `10de:2203`, subsystem `3842:4985`) on an ASUS ROG STRIX Z790-A GAMING WIFI D4**, contributed by **steezecake321** downstream of [JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada). It completes the pre-driver firmware transaction and starts the unmodified Windows Boot Manager in the same boot.

**Hardware result, November 11, 2026:** one successful complete unlock boot (`RUN0020`), Windows return and matched native cuBLAS measurements. The device was then returned to stock by owner-reported full shutdown and direct Windows boot. The unchanged benchmark executable measured:

| Dense 4096³ GEMM | Stock TFLOP/s | Unlocked TFLOP/s | Speedup |
|---|---:|---:|---:|
| FP64, pedantic | 0.566 | 0.583 | +3.1% |
| FP32, pedantic | 28.46 | 29.29 | +2.9% |
| TF32 enabled | 36.54 | 70.73 | **+93.6%** |
| FP16 / FP32 accumulation | 70.43 | 131.69 | **+87.0%** |
| BF16 / FP32 accumulation | 71.51 | 136.60 | **+91.0%** |

Medians of five timed batches, dense inputs, no sparsity. All 64 CPU-reference spot-checks per mode passed in both states with zero sampled error. These are matrix-operation measurements, not instruction issue rates. Application tests improved in four of five workloads; all 472 paired output frames/images matched decoded RGB pixels exactly. Read the [evidence and limits](../../docs/evidence/ga102-z790-windows/README.md) and [benchmark instructions](benchmark/README.md).

## Scope and current status

Tested software: Windows 11 build 26200, NVIDIA 621.14, GPU vBIOS `94.02.a0.00.5e`, 24 GiB, 450 W limit; Intel Core i5-13600K. The motherboard BIOS revision was not recorded in this publication. Driver and power settings were unchanged between states.

This is an exact captured profile, not a universal 3090 Ti installer. Device identity alone does not admit a card. GPU/ROM/FWSEC data, cold-state selectors, firmware hashes, PCI layout, loaded EFI driver code and display-ownership graph are checked. The Windows boot manager and partition geometry are also pinned. Never remove or widen a guard to make another machine pass.

The successful EFI was signed with an owner's private certificate; **the application still explicitly requires Secure Boot off**. Neither the key, certificate, private ESP GUID nor any EFI binary is published. Secure Boot trust/enrollment is not configured by this source.

The source retains historical development comments. The status and measured scope on this page supersede older comments describing stubs or work not yet completed.

## What was corrected

- The original GA102 call site omitted `SS0_WRITTEN -> SS1_WRITE -> TARGET`, despite building with `GU_ENTRY_LATCHED_SS1_FULL`. The guard rejected SS1 before its hardware callback. The corrected sequence also requires verified payload completion and an immediate entry-selector check.
- Faulted normal MMIO/PCI wrappers return synthetic errors. A separate, fixed-address, read-only identity probe now reports actual EFI statuses without clearing the fault or authorizing continuation. Inaccessible selector readback is terminal; there is no write retry.
- Successful post-FLR containment must reach both `work->quiesced` and platform `Gu.quiesced`. `GuQuiesced()` now runs only after all final checks pass. Previously, DMA cleanup correctly refused to free firmware-visible buffers even though the transaction's internal result was zero. The corrected path releases every mapping before Windows handoff.

Failure containment remains mandatory: faults do not authorize cleanup, and uncertain live DMA stays allocated until complete power-off. See [DEVELOPMENT.md](DEVELOPMENT.md).

## Build and test without accessing a GPU

Requirements: Windows x64, Python 3, MinGW-w64 GCC with `ar` and `nm`, and Visual Studio 2022 x64 Build Tools for the EFI linker. Verified here with GCC 16.2 and the installed MSVC linker. CUDA is needed only for the native performance benchmark.

From this directory:

```powershell
python tools/run_host_tests.py
.\Build.ps1 -Python python
```

The host tests use mocks; they do not open or map a physical GPU. The archive build is freestanding and has no EFI boot entry. The full captured-PROM/asset-dependent integration suites are not included; the `-RunTests` legacy switch refuses clearly rather than claiming they ran.

A locally configured EFI build additionally requires a private header:

```powershell
New-Item -ItemType Directory -Force local
Copy-Item boot_profile.example.h local/boot_profile.h
# Independently identify/review the actual Windows ESP GUID and replace the zero example.
.\Build-Experimental.ps1 -Python python
```

The example's all-zero GUID is deliberately unusable. The GUID is only one check: the captured partition geometry and boot-manager hash in `boot_io.c` must also match. A different layout needs independently reviewed captures, not a guessed configuration.

`build/GA102-EXPERIMENTAL.EFI` is GPU-write-capable. Build success verifies structure, not hardware suitability. The build does not sign, stage, boot, change firmware variables or alter the Windows boot order. Generated manifests mark that new build as hardware-untested; the historical result is separately recorded under `docs/evidence/`.

The [read-only probe](probe/README.md) is separate from the transaction application and has its own build. Its raw captures contain ROM data and identifiers and must remain private.

## Firmware and manual USB layout

No NVIDIA firmware, generated payload, PROM/VBIOS, loaded EFI driver image or Windows executable is distributed. Follow the repository's [pinned firmware workflow](../../firmware/README.md) and [licensing notice](../../firmware/NOTICE.md). This port enforces the five GA102 names, lengths and hashes in `profile.c`; substitutes are rejected. Downloads and asset generation are separate from the source build.

Only after independent profile review, the layout used on the tested machine was:

```text
EFI/BOOT/BOOTX64.EFI       locally built (optionally locally signed) application
GA102-ASSETS/             the five locally generated, pinned GA102 files
GA102-UNLOCK/RUNxxxx.TXT   run logs written by the application
```

There is no automatic staging or boot installer. Preserve any existing USB contents and the stock Windows boot route. A successful experiment used a full shutdown, ten-second wait, manual USB boot and one `U` choice. `S`/Esc or timeout chooses stock Windows. The application deliberately halts if a mutation occurred and safe cleanup cannot be proved; its displayed recovery procedure calls for complete power-off and Windows Boot Manager if Windows does not return after four minutes. No automatic retry is implemented.

## Limits

One successful full hardware attempt and short benchmarks qualify this exact result only. Repeated alternating cold A/B/A boots, extended numerical/stability workloads, sleep/hibernate, warm-reset persistence, firmware updates and other driver versions remain unqualified. The public host tests cover selector policy, post-fault diagnostics, reset containment and DMA lifetime; they are not a complete physical transaction simulator.

No Linux admission logic or other Windows port is changed. Original upstream work, licences and contributor credit are retained: see [NOTICES.md](NOTICES.md).
