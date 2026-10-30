# ============================================================================
# OCTAVOX R17-P2: full Windows/MSVC build + validation chain (PowerShell)
# ============================================================================
# Windows counterpart of build_and_test.sh. Same wrapper source, same gates:
#   1. native core (Release): build + the 85-case suite + the four
#      default-neutrality reference hashes (+ optional bench)
#   2. VST3 wrapper (Release): build (the SDK validator runs automatically
#      as part of the module build), the 15-case wrapper tests and a manual
#      validator re-run against the built bundle
#
# There is NO sanitizer leg on Windows: the ENABLE_SANITIZERS branch in
# native/CMakeLists.txt uses GCC/Clang -fsanitize flags (documented).
#
# Usage (from an elevated/normal "Developer PowerShell for VS" or any shell
# with cl.exe + cmake on PATH):
#   powershell -ExecutionPolicy Bypass -File build_and_test.ps1 `
#       [-SdkRoot <path>] [-EvidenceOut <dir>] [-SkipBench]
#
# Defaults: the SDK at ..\..\..\vst3sdk next to Octavox_RE_evidence (see
# native\tools\get_vst3_sdk.ps1); evidence logs written to .\r17_validation
# (or -EvidenceOut). Exit code 0 iff EVERY gate passes.
#
# !!! STATUS: AUTHORED, NOT EXECUTED !!!
# This environment has NO Windows runtime. This script was written from the
# Linux-verified build chain (build_and_test.sh, executed end-to-end this
# round: 85/85 release + ASan legs, 15/15 wrapper tests, validator 47/47)
# and the VST3 SDK's documented MSVC build support. A Windows build or DAW
# test has NOT been performed -- any claim of a Windows binary would be
# false until this script is actually run on Windows and its output
# captured. Cross-compiler caveat: the four reference hashes are the GCC
# 14.2 record; MSVC codegen may legitimately differ in floating-point
# rounding, so treat a hash mismatch on Windows as a REPORT, not proof of a
# defect (the 85-case suite carries the correctness gates).
# ============================================================================
[CmdletBinding()]
param(
    [string]$SdkRoot = "",
    [string]$EvidenceOut = "",
    [switch]$SkipBench
)

$ErrorActionPreference = 'Stop'

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$NativeDir   = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$Vst3Dir     = Join-Path $NativeDir 'vst3'
if (-not $SdkRoot) {
    $SdkRoot = Join-Path (Split-Path -Parent (Split-Path -Parent (Resolve-Path (Join-Path $ScriptDir '..\..')).Path)) 'vst3sdk'
}
if (-not $EvidenceOut) { $EvidenceOut = Join-Path (Get-Location) 'r17_validation' }
New-Item -ItemType Directory -Path $EvidenceOut -Force | Out-Null

$Script:Failures = 0
function Fail([string]$Message) {
    Write-Host "  !! FAILED: $Message" -ForegroundColor Red
    $Script:Failures += 1
}
$T0 = Get-Date

Write-Host 'OCTAVOX full validation chain -- Windows/MSVC (UNTESTED SCRIPT: authored from the Linux-verified chain)'
Write-Host "  native tree:   $NativeDir"
Write-Host "  evidence out:  $EvidenceOut"
Write-Host ('  started:       {0:u}' -f $T0)
Write-Host '-----------------------------------------------------------------'
Write-Host '  NOTE: no Windows build of this project has been executed or verified.'
Write-Host '        See the header of this script before quoting any result.'
Write-Host '-----------------------------------------------------------------'

# ---- toolchain checks --------------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host 'ERROR: cmake not on PATH (need >= 3.19).' -ForegroundColor Red
    exit 1
}
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    Write-Host 'ERROR: cl.exe (MSVC) not on PATH.' -ForegroundColor Red
    Write-Host '       Open a "Developer PowerShell for Visual Studio 2019/2022" and re-run,'
    Write-Host '       or otherwise import the MSVC environment first.' -ForegroundColor Red
    exit 1
}
Write-Host ("toolchain: " + ((cmake --version | Select-Object -First 1)))
Write-Host ("compiler:  " + ((cl 2>&1 | Select-Object -First 1)))

function Run-Logged([string]$LogFile, [scriptblock]$Cmd) {
    & $Cmd *> (Join-Path $EvidenceOut $LogFile)
    return $LASTEXITCODE
}

# ==============================================================================
# LEG 1: native core, Release
# ==============================================================================
Write-Host ''
Write-Host '[LEG 1] native core -- Release: 85-case suite + 4 reference hashes'
$t = Get-Date
$BuildDir = Join-Path $NativeDir 'build'
cmake -S $NativeDir -B $BuildDir -DCMAKE_BUILD_TYPE=Release *> (Join-Path $EvidenceOut 'configure_release.log')
if ($LASTEXITCODE -ne 0) { Fail 'release configure' }
cmake --build $BuildDir --config Release *> (Join-Path $EvidenceOut 'build_release.log')
if ($LASTEXITCODE -ne 0) { Fail 'release build' }
else { Write-Host '  built (see build_release.log)' }

# locate the built tools (MSVC multi-config layout: build\Release\*.exe)
$TestsExe  = Get-ChildItem -Recurse -File $BuildDir -Filter 'octavox_tests.exe'  -ErrorAction SilentlyContinue | Select-Object -First 1
$RefcapExe = Get-ChildItem -Recurse -File $BuildDir -Filter 'octavox_r15refcap.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
$BenchExe  = Get-ChildItem -Recurse -File $BuildDir -Filter 'octavox_bench.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $TestsExe)  { Fail 'octavox_tests.exe not found after build' }
if (-not $RefcapExe) { Fail 'octavox_r15refcap.exe not found after build' }

if ($TestsExe) {
    & $TestsExe.FullName --json (Join-Path $EvidenceOut 'r17_acceptance_results.json') *> (Join-Path $EvidenceOut 'r17_suite_stdout_release.txt')
    if ($LASTEXITCODE -ne 0) { Fail 'release suite (85/85 required)' }
    $Sum = Select-String -Path (Join-Path $EvidenceOut 'r17_suite_stdout_release.txt') -Pattern '(\d+) passed, (\d+) failed, (\d+) total' | Select-Object -First 1
    if ($Sum -and ($Sum.Matches[0].Groups[2].Value -eq '0')) {
        Write-Host ("  suite: " + $Sum.Matches[0].Value)
    } else {
        Fail 'release suite summary line missing/unexpected'
    }
}

if ($RefcapExe) {
    & $RefcapExe.FullName *> (Join-Path $EvidenceOut 'r17_refcap_check.txt')
    if ($LASTEXITCODE -ne 0) { Fail 'refcap run' }
    # compare the four hashes against native/r15_reference_hashes.txt
    $HashFile = Join-Path $NativeDir 'r15_reference_hashes.txt'
    $HashOk = $true
    foreach ($Cfg in @('cfg1_default_unity_lcg_noise_b64_150_512',
                       'cfg2_r700c_8v_spread_fb025_sine220',
                       'cfg3_rm400c_4v_nospread_chirp200_800',
                       'cfg4_stereo_2ch_4v_unity_then_r400c')) {
        $GotLine  = Select-String -Path (Join-Path $EvidenceOut 'r17_refcap_check.txt') -Pattern $Cfg | Select-Object -First 1
        $WantLine = Select-String -Path $HashFile -Pattern $Cfg | Select-Object -First 1
        $Got  = if ($GotLine)  { ($GotLine.Line  -replace '.*?(0x[0-9a-f]+).*','$1') } else { '(missing)' }
        $Want = if ($WantLine) { ($WantLine.Line -replace '.*?(0x[0-9a-f]+).*','$1') } else { '(missing)' }
        if ($Got -ne $Want) {
            Write-Host ("  reference hash {0}: got {1} want {2}  (cross-compiler FP note: see script header)" -f $Cfg, $Got, $Want) -ForegroundColor Yellow
            $HashOk = $false
        }
    }
    if ($HashOk) { Write-Host '  neutrality hashes: 4/4 exact match vs r15_reference_hashes.txt' }
    else { Write-Host '  neutrality hashes: MISMATCH vs the GCC record (documented cross-compiler caveat -- the suite carries the gates)' -ForegroundColor Yellow }
}

if (-not $SkipBench -and $BenchExe) {
    & $BenchExe.FullName --json (Join-Path $EvidenceOut 'r17_bench.json') *> (Join-Path $EvidenceOut 'r17_bench_stdout.txt')
    if ($LASTEXITCODE -ne 0) { Fail 'bench run' }
    Write-Host '  bench: completed (see r17_bench.json)'
}
Write-Host ("  leg 1 wall: {0} s" -f [int]((Get-Date) - $t).TotalSeconds)

# ==============================================================================
# LEG 2: the VST3 wrapper (SDK + module + wrapper tests + validator)
# ==============================================================================
Write-Host ''
Write-Host '[LEG 2] VST3 wrapper -- module + 15-case wrapper tests + SDK validator'
$t = Get-Date
if (-not (Test-Path (Join-Path $SdkRoot 'CMakeLists.txt'))) {
    Write-Host "  ERROR: VST3 SDK not found at '$SdkRoot'." -ForegroundColor Red
    Write-Host '  Run native\tools\get_vst3_sdk.ps1 first (pinned SHAs), or pass -SdkRoot <path>.' -ForegroundColor Red
    Fail 'SDK presence'
} else {
    Write-Host "  SDK: $SdkRoot"
    $VBuild = Join-Path $Vst3Dir 'build'
    cmake -S $Vst3Dir -B $VBuild -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT=$SdkRoot *> (Join-Path $EvidenceOut 'configure_vst3.log')
    if ($LASTEXITCODE -ne 0) { Fail 'vst3 configure' }
    cmake --build $VBuild --config Release *> (Join-Path $EvidenceOut 'build_vst3.log')
    if ($LASTEXITCODE -ne 0) { Fail 'vst3 build (incl. the automatic validator pass)' }
    else { Write-Host '  built; the automatic validator pass is inside build_vst3.log' }

    $WrapTests = Get-ChildItem -Recurse -File $VBuild -Filter 'octavox_vst3_wrapper_tests.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($WrapTests) {
        & $WrapTests.FullName *> (Join-Path $EvidenceOut 'r17_vst3_wrapper_tests.txt')
        if ($LASTEXITCODE -ne 0) { Fail 'wrapper tests (15/15 required)' }
        $WSum = Select-String -Path (Join-Path $EvidenceOut 'r17_vst3_wrapper_tests.txt') -Pattern '(\d+) passed, (\d+) failed, (\d+) total' | Select-Object -First 1
        if ($WSum -and ($WSum.Matches[0].Groups[2].Value -eq '0')) {
            Write-Host ("  wrapper tests: " + $WSum.Matches[0].Value)
        } else {
            Fail 'wrapper test summary unexpected'
        }
    } else {
        Fail 'octavox_vst3_wrapper_tests.exe not found after build'
    }

    $Validator = Get-ChildItem -Recurse -File $VBuild -Filter 'validator.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    $ModuleDir = Get-ChildItem -Recurse -Directory $VBuild -Filter 'OctavoxNative.vst3' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Validator -and $ModuleDir) {
        & $Validator.FullName $ModuleDir.FullName *> (Join-Path $EvidenceOut 'r17_vst3_validator.txt')
        if ($LASTEXITCODE -ne 0) { Fail 'SDK validator (47/47 required)' }
        $VSum = Select-String -Path (Join-Path $EvidenceOut 'r17_vst3_validator.txt') -Pattern '(\d+) tests passed, (\d+) tests failed' | Select-Object -First 1
        if ($VSum) { Write-Host ("  validator: " + $VSum.Matches[0].Value) }
        $ModuleDll = Get-ChildItem -Recurse -File $ModuleDir.FullName -Filter 'OctavoxNative.vst3' -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $ModuleDll) { $ModuleDll = Get-ChildItem -Recurse -File $ModuleDir.FullName -Filter 'OctavoxNative.dll' -ErrorAction SilentlyContinue | Select-Object -First 1 }
        if ($ModuleDll) {
            Write-Host ("  module: " + $ModuleDll.FullName)
            Write-Host ("    sha256: " + (Get-FileHash -Algorithm SHA256 $ModuleDll.FullName).Hash.ToLower())
        }
    } else {
        Fail 'validator or module bundle not found after build'
    }
}
Write-Host ("  leg 2 wall: {0} s" -f [int]((Get-Date) - $t).TotalSeconds)

# ==============================================================================
# summary
# ==============================================================================
Write-Host '-----------------------------------------------------------------'
$Total = [int]((Get-Date) - $T0).TotalSeconds
if ($Script:Failures -eq 0) {
    Write-Host "RESULT: ALL GATES PASSED (total wall $Total s)" -ForegroundColor Green
    Write-Host "  evidence captured in: $EvidenceOut"
    Write-Host '  Remember: this script is UNTESTED on Windows -- if you are running it,'
    Write-Host '  YOUR run is the first verification. Capture the output and report it.'
    exit 0
} else {
    Write-Host "RESULT: $($Script:Failures) FAILURE(S) (total wall $Total s)" -ForegroundColor Red
    Write-Host "  evidence captured in: $EvidenceOut"
    exit 1
}
