# SPDX-License-Identifier: GPL-2.0-only
# Builds GA102-PROBE.EFI: a read-only diagnostic that combines, in one boot/
# one log, what the ad103-z790-en-REFERENCE package ships as two separate
# tools (probe/ -- PCI config space, BAR0 registers, VBIOS hash -- and
# ownership_probe/ -- protocol-ownership graph, device paths, driver
# identity). See probe_app.c for exactly what it does and does not touch.
# Same toolchain as ../Build-Experimental.ps1 -- freestanding gcc compile,
# then MSVC link.exe for the final PE/EFI binary.
#
# Usage:  .\Build-Probe.ps1 [-Gcc gcc] [-VcVars64 <path to vcvars64.bat>]
#
# Needs: Windows x64, GCC/MinGW-w64 with ar, and MSVC x64 linker tools
# (VS Build Tools). vswhere discovers MSVC automatically if -VcVars64 is not
# given.
param([string]$Gcc='gcc',[string]$VcVars64)
$ErrorActionPreference='Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
 New-Item -ItemType Directory -Path build -Force | Out-Null
 $edk2='..\vendor\edk2\Include'
 if(!(Test-Path -LiteralPath $edk2)){throw "Expected the vendored EDK2 headers at $edk2 (reused from the parent port, unmodified) -- check the folder layout"}
 $flags=@('-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-DGU_UEFI_PORT','-I.',('-I'+$edk2),('-I'+$edk2+'\X64'),'-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-mno-red-zone','-fshort-wchar')
 $sources=@('port_probe','checks_probe','probe_app','devpath','graph','pehash','runtime','format','sha256')
 foreach($s in $sources) {
  & $Gcc @flags -c ($s+'.c') -o ('build/'+$s+'.o')
  if($LASTEXITCODE -ne 0){throw "Compile failed: $s"}
 }
 $vcvars=$VcVars64
 if(!$vcvars){
  $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if(!(Test-Path -LiteralPath $vswhere)){throw 'Pass -VcVars64 with the MSVC x64 setup script'}
  $vcvars=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Auxiliary\Build\vcvars64.bat') | Select-Object -First 1
 }
 if(!$vcvars -or !(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 tools not found'}
 if($vcvars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]'){throw 'Unsupported command-shell path characters'}
 $objects=@('port_probe','checks_probe','probe_app','devpath','graph','pehash','runtime','format','sha256')
 $lines=@('@echo off',('call "'+$vcvars+'" >nul'),'if errorlevel 1 exit /b 1',('cd /d "'+$PSScriptRoot+'"'),('link /nologo /subsystem:efi_application /entry:UefiMain /machine:x64 /nodefaultlib /incremental:no /fixed:no /dynamicbase:no /nxcompat:no /brepro /out:build\GA102-PROBE.EFI '+(($objects | ForEach-Object {'build\'+$_+'.o'}) -join ' ')),'exit /b %errorlevel%')
 Set-Content -LiteralPath build/link-msvc.cmd -Value $lines -Encoding ascii
 & $env:ComSpec /d /c ('"'+(Join-Path $PSScriptRoot 'build\link-msvc.cmd')+'"')
 if($LASTEXITCODE -ne 0){throw 'EFI link failed'}
 Write-Output 'Built build\GA102-PROBE.EFI -- read only, see probe_app.c for exactly what it touches.'
} finally {Pop-Location}
