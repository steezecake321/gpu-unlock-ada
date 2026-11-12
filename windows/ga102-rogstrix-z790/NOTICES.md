# Notices and provenance

This downstream port and its new tests/benchmark integration are GPL-2.0-only. The full licence is in LICENSE and at the repository root. Contribution and hardware evidence: steezecake321. Original authorship in source headers remains intact.

- Repository lineage: JustAResearcher/gpu-unlock-ada, itself derived from Zagitta/gpu-unlock and the firmware research credited by those projects. No new exploit or first-ever Windows 3090 Ti claim is made.
- UEFI strategy and checked-I/O/containment code derive through the existing Latence.app `windows/ad104-msi` contribution and the owner's previously published AD103/Z790 port. The original Windows lineage includes impacifist/Windows-GPU-UNLOCK and rough2515-hub/rtx3080-unlock-public. Preserve the accompanying UPSTREAM-WINDOWS-NOTICES.md, LICENSES/ and original notices.
- The supplied GA102 seed reused an AD103 reference privately for comparison; that reference and its machine-specific inputs are not republished here. Only the GA102 production port, diagnostics and relevant tests are added.
- The GA102 kernel transaction and firmware formats derive from the existing upstream source. NVIDIA inputs, generated payloads, PROMs, loaded driver images and Windows binaries remain external, separately licensed inputs. The repository firmware/NOTICE.md applies; hashes do not grant redistribution rights.
- TianoCore EDK II headers retain their BSD-2-Clause-Patent and per-file notices, including vendor/edk2/License.txt. They are declarations used for a freestanding build, not an EDK II runtime binary. Existing upstream provenance identifies the reference header revision as c1415771ac592ad8509351c46cd3cda4f38d7d52; the publication manifest records the actual files supplied in this seed rather than asserting an independently reconstructed Git history for the seed.

Local keys/certificates, ESP GUID configuration, raw captures and generated binaries are excluded. The public release is source and sanitized evidence only.
