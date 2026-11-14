param(
    [string]$Nvcc = "nvcc",
    [string]$VcVars64,
    [string]$Output = (Join-Path $PSScriptRoot "build")
)
$ErrorActionPreference = "Stop"
$benchSource = Join-Path $PSScriptRoot "..\..\..\diagnostics\bench\mma_bench.cu"
$validateSource = Join-Path $PSScriptRoot "..\..\..\diagnostics\bench\validate_gemm.cu"
if (-not (Test-Path -LiteralPath $benchSource) -or -not (Test-Path -LiteralPath $validateSource) -or
    -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot "validate_fp8.cu"))) {
    throw "Expected benchmark sources under repo/diagnostics/bench"
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
if (-not $VcVars64) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $VcVars64 = @(& $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Auxiliary\Build\vcvars64.bat") | Select-Object -First 1
    }
}
if (-not $VcVars64 -or -not (Test-Path -LiteralPath $VcVars64)) {
    throw "MSVC x64 tools not found; pass -VcVars64 with vcvars64.bat"
}
$cmdFile = Join-Path $Output "build-cuda.cmd"
function Invoke-Nvcc([string[]]$Arguments, [string]$Label) {
    $quoted = $Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }
    @('@echo off', ('call "' + $VcVars64 + '" >nul'), 'if errorlevel 1 exit /b 1',
      ('"' + $Nvcc + '" ' + ($quoted -join ' ')), 'exit /b %errorlevel%') |
      Set-Content -LiteralPath $cmdFile -Encoding ascii
    & $env:ComSpec /d /c ('"' + $cmdFile + '"')
    if ($LASTEXITCODE -ne 0) { throw "nvcc failed building $Label ($LASTEXITCODE)" }
}
Invoke-Nvcc @('-O3','-arch=sm_89','-std=c++17','-o',(Join-Path $Output "mma_bench.exe"),$benchSource) "mma_bench.exe"
Invoke-Nvcc @('-O3','-arch=sm_89','-std=c++17','-o',(Join-Path $Output "validate_gemm.exe"),$validateSource,'-lcublas') "validate_gemm.exe"
Invoke-Nvcc @('-O3','-arch=sm_89','-std=c++17','-o',(Join-Path $Output "validate_fp8.exe"),(Join-Path $PSScriptRoot "validate_fp8.cu")) "validate_fp8.exe"
Get-FileHash -Algorithm SHA256 (Join-Path $Output "mma_bench.exe"), (Join-Path $Output "validate_gemm.exe"), (Join-Path $Output "validate_fp8.exe")
