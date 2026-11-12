# Development and verification

## Regression history

1. Original selector call site: guard rejects SS1 before hardware access because its phase is still ENTRY. Normal post-fault read wrappers then manufacture all-ones/error responses. These responses do not establish a PCIe blackout.
2. After correcting the phase sequence: RUN0019 reaches target selectors, FLR and successful post-FLR readbacks, but stops at the firmware-transaction safety gate. Internal quiescence is not propagated to platform Gu.quiesced; 20,612 DMA mappings remain retained. No Windows start occurs.
3. Corrected containment publication: RUN0020 records `result=0 safe=1 retained=0 quiesced=1 flrs=1 DMA_live=0` and TRANSACTION OK. Logging closes before the final Windows check. The owner's Windows return and subsequent benchmarks provide the additional handoff evidence; the log alone does not record Windows execution.

## Public tests

`python tools/run_host_tests.py` runs:

- Original selector source-order/pre-write/probe wiring checks and five C guard/diagnostic tests.
- An independent C harness checking blocked writes, one-shot consumption, failed-callback retry rejection, faulted normal reads, diagnostic statuses and retained active DMA.
- The actual reset-function body extracted from vendor/unlock_610.c, compiled against mocked device operations and the real port.c DMA/quiescence guards. It exercises GA102 and AD102-family success paths, 51 injected access failures, enabled WPR2, wrong selectors, signature count and a late fault. The original pre-fix source fails its success assertion. Modeled success permits mocked Unmap/FreeBuffer; failures deny both.

The reset harness isolates one function and selected work fields. It does not simulate the entire production transaction, PCI bus, firmware or display graph. Existing private-PROM-dependent full-flow suites were absent from the seed and were not run.

Build verification compiles the library and EFI-entry sources with warnings treated as errors, verifies only the expected external GuLog archive symbol, and checks x64 EFI structure, relocations and absence of imports/TLS/CLR. Maximum reported individual stack frame was 2,128 bytes; this is not a total runtime stack bound.

## Publication changes

Runtime C sources are preserved from the successfully exercised project. Publication adds documentation, evidence, source-only benchmarks, privacy exclusions and a unified host-test entry point; it makes the legacy missing-fixture switch fail clearly. It does not change hardware thresholds, write order, boot guards, diagnostics or DMA policy.
