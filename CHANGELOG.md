# Change log

## 0.7.0 — 2026-10-24 KST
- First actually compiled and executed native Windows ZIP CLI from the supplied v0.6 source.
- Repair portable Zig/CMake/Ninja bootstrap, archiver tool wrappers, short build/cache paths, pinned verified source/tool downloads; vendor current libdeflate 1.26, zlib 1.3.2 and 7-Zip 26.05 sources.
- Strengthen ZIP header/ZIP64/descriptor/extra/overlap/CRC/stream-consumption validation and filename safety.
- Shared fresh-output transaction, guarded directories, no-overwrite handle-relative commit, failure rollback and cancellation; explicit keep-partial mode.
- Real 7z and RAR4/5 SDK adapters to the common sink, including solid archives and explicit unsupported-feature errors.
- Unified list/test/selftest/tune/help/version CLI with distinct exit categories; Korean native GUI, file/open/drop flow, progress, cancel, selftest and AutoTune.
- Strict environment-bound ZIP profiles, bounded calibration candidates, randomized measurements and raw statistics.
- Add actual Windows regression, independent UnRAR SHA256 goldens, large-file, GUI, permission, junction and sanitizer evidence. See validation matrix for exact completed tests and exclusions.
