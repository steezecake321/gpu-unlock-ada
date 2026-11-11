# SPDX-License-Identifier: GPL-2.0-only
# Signs build\AD103-EXPERIMENTAL.EFI (built with Build-Experimental.ps1
# -Signed, so GU_SIGNED_BUILD is defined -- see app.c's v0.1.2 note) with
# the local self-signed cert from local/signing/ (New-SigningKey.ps1), so
# it can run under Secure Boot once that cert's public half (the .cer) is
# enrolled in this board's firmware db. See ../SECURE_BOOT.md.
#
# Ordinary PE/Authenticode signing -- the same signtool call used to sign
# any other Windows binary. UEFI Secure Boot verifies this exact signature
# format; nothing EFI-specific here.
param(
 [string]$SignTool,
 [string]$Pfx = 'local/signing/gpu-unlock-signing.pfx',
 [string]$PassFile = 'local/signing/gpu-unlock-signing.pfx.pass',
 [string]$Target = 'build/AD103-EXPERIMENTAL.EFI'
)
$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath (Join-Path $PSScriptRoot '..')
try {
 if (!(Test-Path -LiteralPath $Pfx)) { throw "No signing key at $Pfx -- run tools\New-SigningKey.ps1 first" }
 if (!(Test-Path -LiteralPath $PassFile)) { throw "No password file at $PassFile (expected alongside $Pfx)" }
 if (!(Test-Path -LiteralPath $Target)) { throw "No binary to sign at $Target -- run Build-Experimental.ps1 -Signed first" }
 $tool = $SignTool
 if (!$tool) {
  $kitsRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots' -Name KitsRoot10 -ErrorAction SilentlyContinue).KitsRoot10
  if ($kitsRoot) {
   $tool = Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'bin') -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
  }
 }
 if (!$tool -or !(Test-Path -LiteralPath $tool)) { throw 'signtool.exe not found -- pass -SignTool <path> (Windows SDK: <KitsRoot>\bin\<version>\x64\signtool.exe)' }
 if ($tool -match '["&|<>%^!\r\n]') { throw 'Unsupported command-shell path characters in -SignTool' }
 $pass = (Get-Content -LiteralPath $PassFile -Raw).Trim()
 & $tool sign /fd SHA256 /f $Pfx /p $pass $Target
 if ($LASTEXITCODE -ne 0) { throw 'signtool sign failed' }
 Write-Output "Signed: $Target"
 Write-Output '(signtool verify will report "not trusted" on this PC until the .cer is in a Windows trust store -- that is expected and unrelated to firmware Secure Boot trust, which only cares whether the .cer is enrolled in this board''s own db. See ../SECURE_BOOT.md.)'
} finally { Pop-Location }
