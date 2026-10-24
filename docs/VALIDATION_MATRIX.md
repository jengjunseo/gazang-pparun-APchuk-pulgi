# Windows validation — v0.7.0

Executed on the supplied Ryzen 3 4100 Windows 11 x64 PC. These are observed results, not a certification for all Windows installations.

| Check | Observed result | Evidence |
|---|---|---|
| Release native build and link | Passed, Zig/Clang 22, C++20, x64 | logs/build-final.log; logs/bootstrap-build.log |
| CTest | 7/7 groups passed | logs/ctest-final.log; bootstrap-build.log |
| Parser mutation corpus | 25,000 mutations passed | CTest; logs/ubsan-run.log |
| Windows UBSan | Passed, no reported undefined behavior in mutation run | logs/ubsan-build.log; ubsan-run.log |
| Windows ASan | Attempted, unavailable linker runtime | logs/asan-build.log |
| Complete CLI corpus | 68/68 cases passed | logs/e2e-results.json; e2e-verified.log |
| CLI error categories | 6/6 checks passed (unsupported signature vs inaccessible input, x/list/test) | logs/cli-status-results.json |
| Failure/profile corpus | 15/15 cases passed | logs/failure-results.json |
| Native GUI | 10/10 checks passed; real 150% DPI rendering inspected | logs/gui-results.json; gui-preview.png |
| Quick/full ZIP tuning | Every measured trial verified; 24 quick and 8 full trials | logs/autotune/ |
| 7-Zip comparison | All 8 runs, including warmups, verified with SHA256 | logs/benchmark-comparison.json |

The complete corpus checks Store/Deflate, UTF-8/CP949, descriptor and ZIP64 layouts, empty files, deterministic renaming, traversal, links, corruption, unsupported methods, 10,104 files, and an actually extracted **2 GiB + 4,096 byte ZIP64 entry**. Names, count, size and SHA256 are checked against generated source bytes. Input SHA256 is checked again after extraction.

7z fixtures cover LZMA and LZMA2, solid/non-solid, Unicode names, truncation and encrypted input rejection. RAR4/RAR5 fixtures cover solid archives, subdirectories, comments, damaged input and link rejection. Seven positive RAR goldens were independently extracted with the signed official **RARLAB UnRAR 7.30 beta 1**, with executable hash and fixture origins retained in tests/fixtures. The runtime uses 7z.dll; the independent verifier is not shipped as part of the application.

Failure checks exercise a private test junction, a private directory with temporarily denied create permissions (restored afterward), oversized declarations, malformed profiles, environment mismatch, existing output refusal, cancellation/rollback and preservation of unrelated orphan sessions. Selftest also exercises subdirectory mutation and rename sharing protection.

GUI automation operates only the application's own process/window. It checks Korean controls, file opening through the real HDROP handler, output bytes, streaming cancellation, fit of controls at actual DPI, real nonblank PrintWindow capture and normal exit. A physical mouse drag, every display scale and moving between monitors were not separately tested.

## Remaining validation limits

- Physical full disk, power loss, device removal, actual CloseHandle failures, and adversarial same-user process races are not covered by these results. The fresh-root/pinning design and its remaining path-open windows are described in SECURITY_REVIEW.md.
- No current Linux sanitizer, ThreadSanitizer, MSVC build, Windows 10 installation or second physical PC result is claimed. Existing portable tests are preserved.
- Outputs are closed and verified before ordinary success. No durable flush or power-loss persistence claim is made.
- Passwords, split volumes, network destinations, links and unsupported ZIP methods are explicitly rejected.
- This portable release is unsigned. There is no installer or Explorer registration. Broader consumer rollout still needs signing and testing on additional PCs.

## Reproduction

Run BUILD_WINDOWS_ONE_CLICK.cmd for the native build and CTest, then RUN_SELFTEST_AND_TUNE.cmd for the integrated selftest and quick tuning. The Python corpus runners in tests require Python 3.11+; GUI capture additionally uses Pillow. e2e_windows.py accepts --help and needs the matching official 7-Zip command-line tool to generate 7z fixtures. Failure tests touch only their newly generated directories. Benchmark and failure scripts retain owned-session markers rather than cleaning unrelated paths.
