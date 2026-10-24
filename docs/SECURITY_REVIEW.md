# Security review — v0.7.0

## Implemented and exercised
- Reject traversal, absolute/drive/UNC/device/ADS paths, embedded NUL and malformed UTF-16. Bound component/path/depth/count and declared/actual bytes; deterministic case-insensitive collision names use CREATE_NEW.
- Never materialize symbolic/hard links, reparse objects or special files. SDK RAR4 POSIX-mode links have an explicit regression after detecting upstream's packed attribute representation.
- Validate ZIP local/central filename, method, flags, CRC and sizes; validate extra-field layout/duplicates, ZIP64 footer and descriptors; reject intersecting local records and unused DEFLATE tails.
- Before file output, use a fresh root and hold subdirectory handles that deny write/delete sharing. Mutation and rename attempts are exercised by selftest. Attribute-only handles were found insufficient by the actual test and replaced.
- Destination ancestor reparse points are refused. Completion uses a directory-handle-relative native rename with no overwrite. All close jobs are drained before publishing. Ordinary failure/cancel rolls back only a random owned session. An explicit keep-partial option preserves evidence.
- Process memory cap 1.5 GiB, ZIP logical fast-payload budget 512 MiB (reused vector capacity can retain more; the process cap remains authoritative), streaming for large entries, disk free-space reserve and 64-bit checked totals. SDK output cannot exceed declared per-entry size.
- Bound profile size to 16 KiB; reject duplicate keys, malformed/trailing JSON, coercions and extreme settings; invalidate mismatched environment/workload. Official DLL loads adjacent to the program; no PATH DLL search.
- No private file content/password/path in normal error logs; user-requested list and keep-partial display names/paths by design. No telemetry or network behavior in extraction.

## Scope and residual limits
- A hostile process with the same user's privileges, an administrator, kernel driver or compromised decoder can already alter user files. Fresh-root/pinned-path protections are not an OS sandbox or a proof against every such attacker. No existing-directory extraction mode is provided.
- Session creation and final cleanup still have small path-open windows; cleanup refuses reparse objects and pins each directory while descending, but is not a complete formal handle-relative filesystem implementation. Do not use a concurrently adversarial shared destination.
- Forced termination/power loss can leave an owned .hx session. Rerun allocates a new session and does not sweep old directories. Physical storage durability is not promised by ordinary completion.
- Windows GNU Zig lacks a linked ASan runtime in this environment: the actual attempted ASan build failed with missing __asan symbols. Windows UBSan and 25,000 parser mutations passed. No current Linux sanitizer run or ThreadSanitizer result is claimed.
- Defender status is read from accessible registry data; unknown is preserved as unknown. A protection-state change that remains unobservable cannot be detected by that fingerprint.
- No encryption/password entry, multivolume extraction, UNC/network destination, installer, Explorer registration or code-signing certificate. Unsupported content fails explicitly.
- Actual full-disk/power-loss/CloseHandle fault injection has not been performed; validation documents the tested alternatives rather than counting these as passes.

Third-party security and redistribution notices are preserved in third_party/7zip/DOC, libdeflate126 and zlib132. Official sources, pinned versions and hashes are recorded. No RAR encoder is implemented or used for data generation.
