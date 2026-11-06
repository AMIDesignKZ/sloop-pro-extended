# Archived legacy engine tests

These files are retained for historical reference only. They target the original multi-engine SLOOP DSP and depend on synth engines that System 37 does not build (SAMPLE, GRAIN, PHYS and NOISE).

They are not part of `tests/run_tests.sh`, are not compiled by the System 37 build, and should not be used as evidence of current System 37 behaviour. The maintained host suite exercises the supported ANALOG and FM6 engines, the dedicated sampled/synthesised drum kits, and the System 37 project formats.

If one of these tests is revived, first rewrite it around the current engine table and drum/sample architecture, then add it to `tests/run_tests.sh`.
