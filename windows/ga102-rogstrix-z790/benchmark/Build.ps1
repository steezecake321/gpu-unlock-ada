# SPDX-License-Identifier: GPL-2.0-only
param([string]$CudaRoot='C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.5',[string]$VcVars64)
$ErrorActionPreference='Stop'
$nvcc=Join-Path $CudaRoot 'bin/nvcc.exe'
if(!(Test-Path -LiteralPath $nvcc)){throw 'CUDA compiler not found; pass -CudaRoot for the reviewed toolkit.'}
if(!$VcVars64){
 $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
 if(!(Test-Path -LiteralPath $vswhere)){throw 'Pass -VcVars64 for the installed MSVC x64 setup script.'}
 $VcVars64=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Auxiliary/Build/vcvars64.bat') | Select-Object -First 1
}
if(!$VcVars64 -or !(Test-Path -LiteralPath $VcVars64)){throw 'MSVC x64 tools not found'}
foreach($path in @($VcVars64,$PSScriptRoot,$CudaRoot)){if($path -match '["&|<>%^!\r\n]'){throw 'Unsupported command-shell path characters'}}
New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot build) | Out-Null
$source=Join-Path $PSScriptRoot 'gemm_bench.cu';$binary=Join-Path $PSScriptRoot 'build/gemm_bench.exe'
$cmd=Join-Path $PSScriptRoot 'build/build-native.cmd'
@('@echo off',('call "'+$VcVars64+'" >nul'),'if errorlevel 1 exit /b 1',('"'+$nvcc+'" -O3 -std=c++17 -arch=sm_86 "'+$source+'" -lcublas -o "'+$binary+'"'),'exit /b %errorlevel%') | Set-Content -LiteralPath $cmd -Encoding ascii
& $env:ComSpec /d /c ('"'+$cmd+'"')
if($LASTEXITCODE -ne 0){throw 'Native compilation failed; verify that the MSVC Windows SDK/UCRT is installed.'}
Get-FileHash -LiteralPath $binary
