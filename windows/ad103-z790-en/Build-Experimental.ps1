# SPDX-License-Identifier: GPL-2.0-only
# Adapted from ../ad104-msi/Build-Experimental.ps1 -- same flags/logic,
# msi_release -> z790_release, output renamed AD103-EXPERIMENTAL.EFI.
# Needs MSVC x64 linker tools (VS Build Tools) in addition to Build.ps1's
# MinGW GCC; vswhere discovers MSVC automatically if -VcVars64 isn't given.
#
# -Signed (2026-11-10, owner's explicit request, see SECURE_BOOT.md): adds
# -DGU_SIGNED_BUILD (flips app.c's SecureBoot check to require SB *on*,
# see its v0.1.2 note) and Authenticode-signs the linked .EFI with
# tools/Sign-Efi.ps1 using the local key from tools/New-SigningKey.ps1.
# Without -Signed this is byte-for-byte the same build as before -- the
# reviewed unsigned/SB-off artifact is unchanged.
#
# -Resident (2026-11-11, owner's explicit request): adds -DGU_RESIDENT_BOOT,
# which makes boot_io.c's GuBootOpen() accept this board's pinned internal
# partition (resident_boot_profile.h / local/resident_boot_profile.h)
# instead of requiring removable USB media. See boot_io.c's GU_RESIDENT_BOOT
# comment for the safety trade-off this makes (no more physical unplug
# kill-switch). Independent of -Signed; combine both for a signed binary
# that also runs from the resident partition.
param([string]$Gcc='gcc',[string]$Python='python',[string]$VcVars64,[switch]$Signed,[string]$SignTool,[switch]$Resident)
$ErrorActionPreference='Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
 & .\Build.ps1 -Gcc $Gcc -Python $Python
 if(!$?){throw 'Core build failed'}
 $flags=@('-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-D__KERNEL__','-DGU_UEFI_PORT','-DGU_AD104_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL','-Icompat','-Ivendor/edk2/Include','-Ivendor/edk2/Include/X64','-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-mno-red-zone','-fshort-wchar','-fstack-usage')
 if($Signed){$flags+='-DGU_SIGNED_BUILD'}
 if($Resident){$flags+='-DGU_RESIDENT_BOOT'}
 foreach($s in @('app','boot_io','z790_release')) {
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
 if($vcvars -match '["&|<>%^!\r\n]'){throw 'Unsupported command-shell path characters'}
 if($PSScriptRoot -match '["&|<>%^!\r\n]'){throw 'Unsupported command-shell path characters'}
 $objects=@('app','boot_io','z790_release','port','compat','checks','profile','sha256','runtime','format','vendor_unlock_610','vendor_prep_610','vendor_layout_610','vendor_vbios_610','vendor_gfw_eval')
 $lines=@('@echo off',('call "'+$vcvars+'" >nul'),'if errorlevel 1 exit /b 1',('cd /d "'+$PSScriptRoot+'"'),('link /nologo /subsystem:efi_application /entry:UefiMain /machine:x64 /nodefaultlib /incremental:no /fixed:no /dynamicbase:no /nxcompat:no /brepro /out:build\AD103-EXPERIMENTAL.EFI '+(($objects | ForEach-Object {'build\'+$_+'.o'}) -join ' ')),'exit /b %errorlevel%')
 Set-Content -LiteralPath build/link-msvc.cmd -Value $lines -Encoding ascii
 & $env:ComSpec /d /c ('"'+(Join-Path $PSScriptRoot 'build\link-msvc.cmd')+'"')
 if($LASTEXITCODE -ne 0){throw 'EFI link failed'}
 & $Python tools/verify_experimental.py
 if($LASTEXITCODE -ne 0){throw 'EFI verification failed'}
 $signingCert=$null
 if($Signed) {
  $signArgs=@{}
  if($SignTool){$signArgs['SignTool']=$SignTool}
  & .\tools\Sign-Efi.ps1 @signArgs
  if($LASTEXITCODE -ne 0){throw 'Signing failed'}
  $cerPath='local/signing/gpu-unlock-signing.cer'
  if(Test-Path -LiteralPath $cerPath){
   $signingCert=(Get-PfxCertificate -FilePath $cerPath).Thumbprint
  }
 }
 $hashes=[ordered]@{}
 Get-ChildItem -LiteralPath . -Recurse -File | Where-Object {$_.Extension -in @('.c','.h','.ps1','.py') -and $_.FullName -notlike '*\build\*' -and $_.FullName -notlike '*\local\*'} | Sort-Object FullName | ForEach-Object {$hashes[$_.FullName.Substring($PSScriptRoot.Length+1)]=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
 # automatic_unlock/default changed 2026-11-10: GuBootChoose() now defaults
 # to unlock after 5s instead of defaulting to skip after 30s -- see
 # boot_io.c's header comment and app.c's v0.1.1 note. This manifest field
 # must track that behavior accurately, not just the binary's hash.
 # signed/secure_boot_required (v0.1.2): -Signed builds with GU_SIGNED_BUILD
 # and Authenticode-signs the result -- sha256 below is read AFTER signing,
 # since signing changes the file's bytes. See app.c's v0.1.2 note.
 [ordered]@{version='0.1.2';mode='auto-unlock-experimental-one-physical-attempt';binary='AD103-EXPERIMENTAL.EFI';sha256=(Get-FileHash build/AD103-EXPERIMENTAL.EFI).Hash.ToLowerInvariant();gpu_write_capability=$true;hardware_tested=$false;previous_hardware_run=$null;automatic_unlock=$true;requires_key=$null;bypass_key='S or Esc (immediate, within the 5s window)';default='auto-unlock after 5s';watchdog_seconds=300;signed=[bool]$Signed;secure_boot_required=[bool]$Signed;signing_cert_thumbprint=$signingCert;resident_boot=[bool]$Resident;source_hashes=$hashes;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content build/experimental-manifest.json -Encoding utf8
} finally {Pop-Location}
