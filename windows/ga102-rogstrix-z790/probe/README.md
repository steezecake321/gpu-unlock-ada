# Read-only GA102 preboot probe

Build with `./Build-Probe.ps1` on the documented GCC/MSVC toolchain. This separate EFI application reads PCI configuration, selected BAR0 registers, PROM contents and the firmware protocol/ownership graph, and writes diagnostic captures to its USB volume. It does not execute the unlock transaction, allocate GPU DMA, write GPU registers, perform FLR, detach drivers, start Windows or change boot variables.

The word read-only refers to GPU/system configuration; the tool does write its output files. Keep raw captures private: they may contain ROM bytes, device paths, identifiers, handles and detailed firmware data. Publish reviewed summaries only. A successful probe does not qualify a new machine for the mutating transaction.
