@echo off
REM ===========================================================================
REM OCTAVOX R19 -- Windows-side validation kit (variant: Lagrange6)
REM ===========================================================================
REM Run this on a REAL Windows x64 machine, from THIS directory
REM (Windows_Validation_Kit\Lagrange6). Expected relative layout:
REM   ..\..\Builds\OctavoxRE_R19_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3
REM Gates: validator 47/47, wrapper tests 15/15, integration tests 12/12.
REM ===========================================================================
setlocal
cd /d "%~dp0"
set MODULE=..\..\Builds\OctavoxRE_R19_Windows_x64\Lagrange6\OctavoxRE_Lagrange6.vst3

echo === [1/3] Steinberg VST3 validator (47 tests expected) ===
..\shared\validator.exe "%MODULE%"
if errorlevel 1 goto :fail

echo === [2/3] octavox wrapper tests (15/15 expected) ===
octavox_vst3_wrapper_tests.exe
if errorlevel 1 goto :fail

echo === [3/3] compiled-module integration tests (12/12 expected) ===
octavox_module_integration_tests.exe --module "%MODULE%" --json integration_results_lagrange6.json
if errorlevel 1 goto :fail

echo.
echo ALL WINDOWS-SIDE GATES PASSED (variant Lagrange6)
exit /b 0

:fail
echo VALIDATION FAILED (see the output above)
exit /b 1
