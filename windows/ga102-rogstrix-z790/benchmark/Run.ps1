# SPDX-License-Identifier: GPL-2.0-only
param([Parameter(Mandatory=$true)][ValidateSet('stock','unlocked')][string]$State,[ValidateRange(256,8192)][int]$Size=4096,[string]$CudaRoot='C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.5')
$ErrorActionPreference='Stop'
if($Size%256){throw 'Matrix size must be a multiple of 256'}
$exe=Join-Path $PSScriptRoot 'build/gemm_bench.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'Build the native benchmark first'}
$dir=Join-Path $PSScriptRoot 'build/results';New-Item -ItemType Directory -Force $dir | Out-Null
$file=Join-Path $dir ("cuda-native-$State.csv");$meta=Join-Path $dir ("env-$State.json")
if((Test-Path -LiteralPath $file) -or (Test-Path -LiteralPath $meta)){throw 'Preserve existing state results before rerunning; overwrite refused'}
$env:PATH=(Join-Path $CudaRoot 'bin/x64')+';'+(Join-Path $CudaRoot 'bin')+';'+$env:PATH
[ordered]@{state=$State;state_source='operator label; hardware state is not changed or detected';matrix_size=$Size;timestamp=(Get-Date -Format o);executable_sha256=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant();gpu=@(& nvidia-smi --query-gpu=name,driver_version,power.limit,temperature.gpu,utilization.gpu --format=csv,noheader)} | ConvertTo-Json | Set-Content -LiteralPath $meta -Encoding utf8
& $exe $Size | Tee-Object -FilePath $file
if($LASTEXITCODE -ne 0){throw "Native benchmark failed: $LASTEXITCODE"}
