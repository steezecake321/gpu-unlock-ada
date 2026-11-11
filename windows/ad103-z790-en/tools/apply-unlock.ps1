# SPDX-License-Identifier: GPL-2.0-only
# One-shot boot into the unlock stick, without touching the permanent
# {fwbootmgr} displayorder (which must stay {bootmgr} first -- see README.md
# "double boot-order" fix) and without mashing F11 at boot.
#
# `bcdedit /set {fwbootmgr} bootsequence {guid}` sets the UEFI firmware's
# one-shot BootNext-equivalent: the NEXT boot only goes to that firmware
# entry; every boot after that falls back to the normal default
# ({bootmgr}) on its own. Confirmed live on this machine 2026-11-10:
# `bcdedit /enum firmware` shows displayorder = {bootmgr} first already,
# with the stick as a separate, not-first Firmware Application entry.
#
# Finds the stick by its firmware entry's drive letter (default F:), so it
# keeps working if the stick is reformatted/replaced -- never hardcodes the
# entry's GUID.
param(
 [string]$Drive = 'F',
 [switch]$Reboot
)
$ErrorActionPreference = 'Stop'

function Test-Admin {
 ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
 $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-Drive', $Drive)
 if ($Reboot) { $args += '-Reboot' }
 Start-Process pwsh -ArgumentList $args -Verb RunAs -Wait
 exit $LASTEXITCODE
}

$enum = bcdedit /enum firmware
if ($LASTEXITCODE -ne 0) { throw "bcdedit /enum firmware failed" }

# Parse into blocks separated by blank lines; find the one whose device line
# says partition=<Drive>: and pull its identifier GUID.
$blocks = ($enum -join "`n") -split "`r?`n`r?`n"
$target = $null
foreach ($b in $blocks) {
 if ($b -match "device\s+partition=$Drive`:") {
  if ($b -match 'identifier\s+(\{[0-9a-fA-F-]+\})') { $target = $Matches[1] }
 }
}

if (-not $target) {
 Write-Host "No firmware boot entry found on $Drive`:. Entries seen:" -ForegroundColor Yellow
 Write-Host $enum
 throw "Stick not found on $Drive`: -- plug it in, or pass -Drive <letter>"
}

Write-Host "Unlock stick firmware entry: $target (drive $Drive`:)"
bcdedit /set '{fwbootmgr}' bootsequence $target
if ($LASTEXITCODE -ne 0) { throw "bcdedit /set bootsequence failed" }
Write-Host "One-shot boot set. The NEXT restart boots the unlock stick once; every boot after that returns to normal Windows automatically (permanent default is unchanged)." -ForegroundColor Green

if ($Reboot) {
 Write-Host "Rebooting now..."
 Restart-Computer
} else {
 Write-Host "Reboot whenever you're ready (or re-run with -Reboot to do it now)."
}
