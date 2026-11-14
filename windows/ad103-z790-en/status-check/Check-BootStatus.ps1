param([switch]$NoPause)
$ErrorActionPreference = 'Stop'
$reader = Join-Path $PSScriptRoot 'boot_status.py'
$python = (Get-Command python -ErrorAction Stop).Source
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    $quoted = '"' + $reader + '"'
    if (-not $NoPause) { $quoted += ' --pause' }
    Start-Process -FilePath $python -ArgumentList $quoted -Verb RunAs -Wait
} else {
    if ($NoPause) { & $python $reader } else { & $python $reader --pause }
}
