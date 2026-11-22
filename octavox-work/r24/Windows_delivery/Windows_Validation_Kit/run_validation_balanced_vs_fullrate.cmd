@echo off
rem ===========================================================================
rem PitchLab Harmonizer -- R24-Windows delivery validation
rem Balanced vs FullRate dual-module null test (run on real Windows x64).
rem
rem Loads BOTH delivered bundles into ONE process (two real LoadLibrary +
rem GetPluginFactory calls) and drives one processor instance from each
rem factory in lockstep with identical stimulus:
rem   * unity (0 st)          -> outputs must be BIT-IDENTICAL (the parked
rem                               path is detector-independent);
rem   * +4 st / -24 st        -> outputs MUST differ (the two builds select
rem                               different YIN detectors; a bit-identical
rem                               result would be a build-configuration
rem                               defect and FAILS the test).
rem Exit code 0 = PASS. Results: out_dual\dual_nulltest.json + WAVs.
rem ===========================================================================
cd /d "%~dp0"
echo PitchLab R24-Windows validation: Balanced + FullRate dual-module null test
echo (loads ..\plugins\PitchLab_Harmonizer_Balanced.vst3 and
echo  ..\plugins\PitchLab_Harmonizer_FullRate.vst3 into this one process)
echo.
octavox_dual_nulltest.exe --lagrange6 "..\plugins\PitchLab_Harmonizer_Balanced.vst3" --fullrate "..\plugins\PitchLab_Harmonizer_FullRate.vst3" --out out_dual --json out_dual\dual_nulltest.json
set EC=%ERRORLEVEL%
echo.
if "%EC%"=="0" (
    echo VALIDATION PASS ^(exit 0^) -- both modules loaded, identities verified,
    echo unity outputs bit-identical, non-unity outputs differ as required.
) else (
    echo VALIDATION FAIL ^(exit %EC%^) -- see the output above and
    echo out_dual\dual_nulltest.json.
)
echo.
echo For full DAW verification steps see README_WINDOWS_VALIDATION.md
pause
exit /b %EC%
