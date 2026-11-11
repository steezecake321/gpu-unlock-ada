# Running AD103-EXPERIMENTAL.EFI with Secure Boot enabled

Added 2026-11-10, owner's explicit request. Everything below is the same
mechanism Linux's shim/MOK uses for a custom kernel module: Secure Boot
stays fully protective against anything else unsigned on this machine; you
are adding exactly one key you generated and control to the firmware's own
trust list (`db`), and signing exactly this one binary with it. Nothing
here disables or weakens Secure Boot itself.

Verified so far on this machine: key generation, the `-Signed` build, and
the Authenticode signing step all work end-to-end (`Get-AuthenticodeSignature`
confirms a valid signature chain to the generated cert; it reports
"not trusted" only because the cert isn't in *Windows'* trust store yet,
which is expected and irrelevant -- see step 2). **Enrolling the cert into
firmware and booting the signed binary under Secure Boot has not been done
yet** -- that's real hardware/firmware state and is left for you to do
deliberately, not run unattended.

## What changed in the code

`app.c`'s `UefiMain` used to unconditionally refuse to run if Secure Boot
reported on (`GetVariable("SecureBoot") != 0 -> EFI_SECURITY_VIOLATION`) --
a deliberate, reviewed check (see `../REVIEW_2026-11-10.md`) enforcing
"this unsigned binary must never run with SB on" in code, not just by
firmware convention.

That check is unchanged for the default (unsigned) build. A new compile
flag, `GU_SIGNED_BUILD` (set by `Build-Experimental.ps1 -Signed`), flips it
to the mirror image: refuse to run if Secure Boot reports **off**. The
signed binary is meant to run *only* under Secure Boot, which is the whole
point of signing it -- see `app.c`'s v0.1.2 header note for the exact
reasoning. Building without `-Signed` produces the byte-identical reviewed
unsigned artifact; nothing about that path changed.

## Step 1 -- generate your own local signing key (done once per machine)

```powershell
.\tools\New-SigningKey.ps1
```

Produces, under `local/signing/` (already `.gitignore`'d via
`/windows/ad103-z790-en/local/` -- never committed):

- `gpu-unlock-signing.pfx` -- private+public. **Never copy this off this
  machine.** Used only by `Sign-Efi.ps1`.
- `gpu-unlock-signing.pfx.pass` -- its random password, generated so
  signing can run unattended. Same handling as the `.pfx`.
- `gpu-unlock-signing.cer` -- public only (DER). **This is the one file
  that's supposed to leave this machine** -- it goes on a USB stick to be
  enrolled into firmware in step 2. Not secret; enrolling it just lets
  firmware verify signatures made by the paired private key, it can't be
  used to forge one.

The generated cert is RSA-3072/SHA-256, self-signed, valid 20 years. The
private key briefly exists in the Windows certificate store during
generation; the script deletes that store entry once the `.pfx` export
succeeds, so the `.pfx` file is the only place it lives afterward.

Re-running with `-Force` generates a replacement key -- you'd need to
re-enroll the new `.cer` and re-sign; the old enrolled key keeps validating
old signed binaries until you separately remove it from the firmware `db`.

## Step 2 -- enroll the public cert into this board's firmware (you do this)

This is real firmware state and real hardware interaction, so it's for you
to do directly, watching each step -- not something to script or run
unattended:

1. Copy `local/signing/gpu-unlock-signing.cer` to the root of a FAT32 USB
   stick.
2. Reboot into UEFI setup (Del/F2 at POST on most boards).
3. Find **Secure Boot → Key Management** (exact wording varies by board
   vendor; MSI typically calls it "Secure Boot" under the Security tab,
   with a "Key Management" or "Image Authentication" sub-page).
4. Choose **"Enroll Signature"** / **"Append db"** (NOT "Clear Secure Boot
   keys" or anything touching PK/KEK -- you only want to add one
   certificate to the `db` allow-list, not replace the platform's own
   keys). Browse to the `.cer` on the USB stick and enroll it.
5. Confirm Secure Boot is still reported as **Enabled** after this (some
   firmware UIs briefly show it as a custom/user-mode state -- that's
   fine and expected once you've added your own key; it should not show
   as *disabled*).
6. Save and exit.

If the board's Secure Boot menu doesn't expose a way to add a single `db`
entry (some only offer "Standard"/"Custom" with a full keys reset), that's
a real limitation of this specific firmware to flag and work through
before going further -- don't fall back to disabling Secure Boot instead,
that defeats the entire point of this change.

## Step 3 -- build and sign

```powershell
.\Build-Experimental.ps1 -Signed
```

This builds with `-DGU_SIGNED_BUILD`, links, then calls
`tools\Sign-Efi.ps1` automatically to Authenticode-sign the result with
your `local/signing/` key. `build\experimental-manifest.json` records
`signed: true`, `secure_boot_required: true`, and the signing cert's
thumbprint, computed from the binary *after* signing.

`tools\Sign-Efi.ps1` looks for `signtool.exe` via the registered Windows
Kits install path; pass `-SignTool <path>` if it can't find one (Windows
SDK's "Signing Tools for Desktop Apps" component -- a few MB, not the full
SDK -- installs it to
`<KitsRoot>\bin\<version>\x64\signtool.exe`).

## Step 4 -- boot it (you do this)

Copy `build\AD103-EXPERIMENTAL.EFI` to the USB stick exactly as before
(`BOOTX64.EFI`, assets under `AD103-ASSETS\`). With Secure Boot back on and
the cert enrolled, firmware should now load and run this signed binary
exactly like any Microsoft-signed one. The boot-time behavior itself is
otherwise unchanged from the auto-unlock flow already in place: 5s
auto-unlock window (S/Esc bypasses to stock Windows, U confirms
immediately), 300s watchdog armed only for the silent-screen transaction
window. See `README.md`'s 2026-11-10 notes for that part.

## Why this is safe to treat as "Secure Boot stays on", not a workaround

Secure Boot's actual job -- refusing to load *anything* whose signature
doesn't chain to a key in `db` -- is completely intact. The only change is
that `db` now also trusts one additional key, which you generated, which
never leaves this machine except as a public certificate, and which only
your own build process can sign with. Anything else unsigned (malware, a
compromised bootloader, anyone else's unsigned tooling) is refused exactly
as before. This is the documented, supported use of UEFI's own key
management, not a bypass of it.
