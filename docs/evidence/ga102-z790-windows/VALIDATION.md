# Publication validation — 2026-11-11

These checks validate the public source package; they are not additional physical GPU runs.

| Check | Result |
|---|---|
| tools/run_host_tests.py | PASS: source wiring, five supplied C tests, independent guard/DMA harness, reset success paths and 51 injected access failures |
| Build.ps1 -RunSelectorTests | PASS: freestanding archive; only external symbol GuLog; no EFI entry |
| Build-Experimental.ps1 | PASS with deliberately unusable all-zero example ESP configuration; x64 EFI structural checks; maximum individual frame 2,128 bytes; never staged or booted |
| probe/Build-Probe.ps1 | PASS: read-only probe build; never staged or booted for publication |
| benchmark/Build.ps1 | PASS with CUDA 13.5.37, installed MSVC x64 tools and Windows SDK; compiled only |
| Python AST / JSON parsing | PASS for all new public files |
| PowerShell parsing | PASS for all new port scripts |
| Production C/header identity | 110 supplied C/header files match the exercised project byte-for-byte |
| Private-input scan | PASS: no local/ or build/ products, private keys/certificates, owner ESP initializer, credentials, personal filesystem/network paths, raw dumps or EFI/executable binaries |
| Benchmark input PNG | RGB 2048x1152; no embedded metadata |

Production files were allowlisted from the working project; the private seed ZIP was not copied. Copied evidence contains selected transaction lines, timings, telemetry and reviewed summaries, not complete captures. New native measurement source is included; generated executables are ignored. Upstream licences and notices remain present.

The original full captured-PROM-dependent integration test fixtures were absent from the seed and are not claimed as exercised. The public source test model does not reproduce actual firmware or Windows startup. Historical hardware evidence is described in README.md and is separate from these publication checks.
