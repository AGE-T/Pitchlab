@echo off
REM ===========================================================================
REM OCTAVOX R19 -- Windows-side dual-module null test (Lagrange6 vs ObsKernel6)
REM ===========================================================================
REM Run this on a REAL Windows x64 machine, from THIS directory (the kit
REM root, Windows_Validation_Kit). Loads BOTH delivered bundles
REM SIMULTANEOUSLY in one process through the pinned VST3 SDK's Windows
REM module loader (LoadLibraryA / GetProcAddress via module_win32.cpp) and
REM drives one processor instance from each factory in lockstep:
REM   * unity        : the outputs must be BIT-IDENTICAL;
REM   * non-unity    : the outputs MUST differ (different kernels engaged).
REM Gate: exit code 0 ("verdict: PASS"). Results: dual_nulltest_out\*.wav +
REM dual_nulltest_out\dual_nulltest.json next to this script.
REM ===========================================================================
setlocal
cd /d "%~dp0"
set LAG=..\..\Builds\OctavoxRE_R19_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3
set OBS=..\..\Builds\OctavoxRE_R19_Windows_x64\ObsKernel6\OctavoxRE_ObsKernel6.vst3

echo === dual-module null test: Lagrange6 vs ObsKernel6 (exit code gates) ===
shared\octavox_dual_nulltest.exe --lagrange6 "%LAG%" --obskernel6 "%OBS%" --out dual_nulltest_out --json dual_nulltest_out\dual_nulltest.json
if errorlevel 1 goto :fail

echo.
echo DUAL-MODULE NULL TEST PASSED (unity bit-identical; non-unity genuinely differs)
exit /b 0

:fail
echo DUAL-MODULE NULL TEST FAILED (see the output above)
exit /b 1
