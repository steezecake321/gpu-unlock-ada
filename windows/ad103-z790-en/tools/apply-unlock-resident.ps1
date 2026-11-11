# SPDX-License-Identifier: GPL-2.0-only
# One-shot boot into the resident internal unlock partition (2026-11-11,
# owner's explicit request -- see boot_io.c's GU_RESIDENT_BOOT comment and
# resident_boot_profile.h), without touching the permanent {fwbootmgr}
# displayorder (must stay {bootmgr} first) and without mashing F11 at boot.
# Same mechanism as tools/apply-unlock.ps1, adapted for a resident partition
# instead of a USB stick: no drive letter to search for, so this finds the
# entry by its firmware description ("AD103 Resident Unlock", set via
# `bcdedit /copy {bootmgr} /d "AD103 Resident Unlock"` when the entry was
# created) rather than by device/drive letter -- never hardcodes the GUID,
# so it keeps working if the entry is ever recreated with a new GUID.
#
# `bcdedit /set {fwbootmgr} bootsequence {guid}` sets the UEFI firmware's
# one-shot BootNext-equivalent: the NEXT boot only goes to that firmware
# entry; every boot after that falls back to the normal default
# ({bootmgr}) on its own.
param(
 [string]$Description = 'AD103 Resident Unlock',
 [switch]$Reboot
)
$ErrorActionPreference = 'Stop'

function Test-Admin {
 ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
 # Start-Process -ArgumentList does NOT auto-quote array elements, so a
 # multi-word value like the default '-Description' ("AD103 Resident
 # Unlock") gets split into separate stray arguments unless each element
 # that might contain a space is pre-quoted here (confirmed as the actual
 # cause of a real "nothing happens" failure 2026-11-11: the elevated child
 # silently received -Description AD103 and two stray extra args, found no
 # matching entry, threw, and the console closed before it could be read).
 $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Description', "`"$Description`"")
 if ($Reboot) { $args += '-Reboot' }
 Start-Process pwsh -ArgumentList $args -Verb RunAs -Wait
 exit $LASTEXITCODE
}

try {
 $enum = bcdedit /enum firmware
 if ($LASTEXITCODE -ne 0) { throw "bcdedit /enum firmware failed" }

 # Parse into blocks separated by blank lines; find the one whose description
 # line matches and pull its identifier GUID.
 $blocks = ($enum -join "`n") -split "`r?`n`r?`n"
 $target = $null
 foreach ($b in $blocks) {
  if ($b -match "description\s+" + [regex]::Escape($Description) + "\s*(\r?\n|$)") {
   if ($b -match 'identifier\s+(\{[0-9a-fA-F-]+\})') { $target = $Matches[1] }
  }
 }

 if (-not $target) {
  Write-Host "No firmware boot entry found with description '$Description'. Entries seen:" -ForegroundColor Yellow
  Write-Host $enum
  throw "Resident unlock entry not found -- was it deleted? Re-run the bcdedit /copy setup, or pass -Description <text>"
 }

 Write-Host "Resident unlock firmware entry: $target ('$Description')"
 bcdedit /set '{fwbootmgr}' bootsequence $target
 if ($LASTEXITCODE -ne 0) { throw "bcdedit /set bootsequence failed" }
 Write-Host "One-shot boot set. The NEXT restart boots the resident unlock partition once; every boot after that returns to normal Windows automatically (permanent default is unchanged)." -ForegroundColor Green

 if ($Reboot) {
  Write-Host "Rebooting now..."
  Restart-Computer
 } else {
  Write-Host "Reboot whenever you're ready (or re-run with -Reboot to do it now)."
 }
} catch {
 # This runs in a freshly elevated, separately-windowed process (see the
 # self-elevation block above) -- without this, an error here just flashes
 # and closes before it can be read (confirmed as the real shape of a real
 # failure 2026-11-11: a silent argument-quoting bug looked like "nothing
 # happened" for exactly this reason). Pause so it's actually readable.
 Write-Host "`nERROR: $_" -ForegroundColor Red
 Read-Host "Press Enter to close"
 exit 1
}
