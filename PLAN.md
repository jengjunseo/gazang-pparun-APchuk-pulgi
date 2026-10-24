# HYPER EXTRACT execution plan
- [x] P0 audit, verified portable tools, native Windows build, baseline tests
- [x] P1 transactional ZIP extraction, strict integrity, cancellation, path defence, regression corpus
- [x] P2 integrated selftest, bounded ZIP AutoTune, environment fingerprint and raw measurements
- [x] P3 official 7-Zip SDK/DLL adapter through shared path and output interfaces; real format fixtures
- [x] P4 lightweight native GUI and portable launcher
- [x] P5 end-to-end validation and measured performance report
- [x] P5 packaging and independent relocated-package smoke check
- Repository publication is recorded by the remote commit, outside this package.
Every completion requires actual command output. Preserve original tests; record untested conditions.

Remaining research/release work: format-specific 7z/RAR calibration; exhaustive batch/threshold and alternative I/O/decoder experiments; statistically stronger calibration; physical disk/power failure injection; cross-PC testing and code signing. These are not counted as completed by the working portable release. See VALIDATION_MATRIX and BENCHMARK_REPORT.
