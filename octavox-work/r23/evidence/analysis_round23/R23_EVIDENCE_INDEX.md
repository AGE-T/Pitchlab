# R23 evidence index (analysis_round23)

| File | Gate | Content |
|---|---|---|
| logs/g0_baseline_suite.json | G0 | R22 baseline clean-build 100/100 |
| logs/g0_baseline_source_sha256.txt | G0 | baseline source snapshot (36 files) |
| logs/g0_environment.txt | G0 | compiler/host record |
| r22_minpitch_legacy_rerun.json | G1 | the legacy instrument re-run (the "before") |
| r23_minpitch_v2.json | G1 | the corrected 360-cell matrix + instrument validation |
| logs/r23_minpitch_v2_stdout.txt | G1 | the full battery table |
| r23_yin_reference.json | G4 | canonical YIN reference: vectors, FFT proof, boundaries |
| r23_detector_compare.json | G5 | 15 signals x 4 rates x 7 variants |
| logs/r23_detector_compare_stdout.txt | G5 | the full comparison table |
| r23_control_battery.json | G6 | the control layer acceptance battery (290 checks) |
| r23_artifacts.json | G7 | the four-symptom reproduction battery |
| logs/r23_vst3_module_integration.json | G7/G8 | the real-module VST3 test (12/12) |
| r23_integration_check.json | G8 | the engine-level integration check (5/5) |
| g3a1_repos.md, g3a2_sources.md, g3b1_librosa.md, g3b2a_aubio.md | G3 | the fetched-source research reports (URL appendices) |
| g1_delta_analysis.py (../tools) | G1 | the legacy-vs-v2 delta script |
