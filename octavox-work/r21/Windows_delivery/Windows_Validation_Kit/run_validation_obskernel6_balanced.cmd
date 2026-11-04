@echo off
rem PitchLab R21 validation: obskernel6_balanced A/B null test
setlocal
set KIT=%~dp0
octavox_dual_nulltest.exe --lagrange6 "%KIT%..\Builds\Lagrange6_Balanced\PitchLab_Lagrange6_Balanced.vst3" --obskernel6 "%KIT%..\Builds\ObsKernel6_Balanced\PitchLab_ObsKernel6_Balanced.vst3"
endlocal
