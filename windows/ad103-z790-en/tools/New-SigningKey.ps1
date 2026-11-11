# SPDX-License-Identifier: GPL-2.0-only
# Generates a local, self-signed Authenticode code-signing certificate used
# to sign AD103-EXPERIMENTAL.EFI so it can run with Secure Boot ENABLED,
# once its public certificate is enrolled into this board's own firmware
# Secure Boot database (db). See ../SECURE_BOOT.md for the full procedure.
#
# This is the same mechanism Linux's shim/MOK uses for a custom kernel:
# Secure Boot stays fully protective against anything else unsigned; you
# are adding exactly one key you generated and control to the firmware's
# own trust list, nothing more.
#
# Output, both under local/signing/ (already .gitignore'd via
# /windows/ad103-z790-en/local/ -- never commit, share, or copy either off
# this machine except the .cer, which is the one meant to leave it):
#   gpu-unlock-signing.pfx       -- private+public. Used by Sign-Efi.ps1.
#   gpu-unlock-signing.pfx.pass  -- the .pfx's random password, generated
#                                   here so signing can run unattended.
#   gpu-unlock-signing.cer       -- public only, DER. THIS is what you
#                                   enroll into firmware (see SECURE_BOOT.md).
#                                   Not secret, but keep it paired with the
#                                   .pfx that actually signs for it -- a
#                                   mismatched enrollment just means
#                                   signatures from this key won't validate.
#
# The private key briefly exists in the Windows certificate store
# (CurrentUser\My) during generation; this script deletes that store entry
# once the .pfx export succeeds, so the .pfx is the only place it lives
# afterward.
param(
 [string]$Subject = 'CN=AD103 Z790 GPU Unlock (local, owner-generated)',
 [int]$YearsValid = 20,
 # 2048, not 3072: confirmed 2026-11-11 that every factory PK/KEK/db cert on
 # this board's own firmware is RSA-2048 -- this firmware's Secure Boot
 # enrollment almost certainly only supports that size (a common limitation
 # of older AMI Secure Boot crypto modules), which is the likely cause of
 # the first enrollment attempt failing with an RSA-3072 cert.
 [int]$KeyLength = 2048,
 [switch]$Force
)
$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath (Join-Path $PSScriptRoot '..')
try {
 $dir = 'local/signing'
 New-Item -ItemType Directory -Path $dir -Force | Out-Null
 $pfx = Join-Path $dir 'gpu-unlock-signing.pfx'
 $passFile = Join-Path $dir 'gpu-unlock-signing.pfx.pass'
 $cer = Join-Path $dir 'gpu-unlock-signing.cer'
 if ((Test-Path -LiteralPath $pfx) -and -not $Force) {
  throw "Already exists: $pfx (pass -Force to generate a replacement -- you'll need to re-enroll the new .cer in firmware and re-sign; the old enrolled key keeps validating old signed binaries until you remove it from db too)"
 }
 $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
  -KeyAlgorithm RSA -KeyLength $KeyLength -HashAlgorithm SHA256 `
  -NotAfter (Get-Date).AddYears($YearsValid) `
  -CertStoreLocation 'Cert:\CurrentUser\My' `
  -KeyExportPolicy Exportable -KeyUsage DigitalSignature
 $bytes = [byte[]]::new(32)
 [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
 $passPlain = [Convert]::ToBase64String($bytes)
 Set-Content -LiteralPath $passFile -Value $passPlain -NoNewline -Encoding ascii
 $securePass = ConvertTo-SecureString -String $passPlain -AsPlainText -Force
 Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $securePass -Force | Out-Null
 Export-Certificate -Cert $cert -FilePath $cer -Type CERT | Out-Null
 Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force
 Write-Output "Generated: $pfx (+ $passFile), public cert: $cer"
 Write-Output "Thumbprint: $($cert.Thumbprint)"
 Write-Output "Next: enroll $cer into this board's firmware Secure Boot db (see ../SECURE_BOOT.md), then run tools\Sign-Efi.ps1 after Build-Experimental.ps1 -Signed."
} finally { Pop-Location }
