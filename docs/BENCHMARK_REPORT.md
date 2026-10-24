# Measured performance — v0.7.0

The design prioritizes file creation/closing, bounded parallel ZIP decoding, per-worker decoder reuse, libdeflate for small entries and zlib streaming for large ones. No antivirus, power-plan, priority or system cache settings were changed.

## Actual process completion comparison

Same synthetic ZIP: **10,104 files, 287,000,000 uncompressed bytes**, 32 folders, deterministic repetitive text; ZIP Deflate level 6. Same NTFS C: output volume and fresh output directory for each run. HX uses W8/K0 with --no-profile; official 7-Zip 26.05 uses -mmt=8. Each tool has one excluded warmup, followed by three randomized paired blocks.

T_user is measured from process creation through successful process exit. HX performs CRC/size checks inside extraction. Both tools' relative paths, counts, sizes and all SHA256 values are independently checked afterward, **outside the timer**. Deleting owned synthetic outputs is also excluded. The archive SHA256 is 3272ee2552da6a6ec897197def0aeb28e4cbfe24ed8e29a14bcdc107c9cd0067.

| Tool | Block 1 | Block 2 | Block 3 | Median |
|---|---:|---:|---:|---:|
| HX W8 K0 | 19.813 s | 16.190 s | 17.721 s | **17.721 s** |
| 7-Zip 26.05, mmt8 | 23.263 s | 23.238 s | 33.225 s | **23.263 s** |

HX's median elapsed time is **23.8% lower in this particular test**. Three measured samples are a small study. The excluded first HX run took **57.319 s**, versus 22.032 s for 7-Zip: initial-run behavior and background storage/scanner activity can dominate. Warm/cold cache state was uncontrolled, and no causation for the variation is claimed. This is not evidence that HX is fastest across archives, machines or cold starts. Bandizip was not measured. RAR/7z extraction performance was not independently compared.

Raw runs, including both warmups, are in logs/benchmark-comparison.json; the reproducible harness is tools/benchmark_windows.py. Input generation is deterministic in content; ZIP timestamps can change the archive hash on another run.

## Automatic tuning on this PC

Quick tuning used 512 synthetic files / 11,999,742 bytes, eight W/K settings, three randomized paired blocks, 24 verified trials. It selected **W8 K4**, median T_engine 1,260.89 ms. Full tuning used 10,104 files / 286,994,987 bytes. The measured trial cost limited the nominal 180-second budget to four settings and two complete blocks, eight verified trials. It selected **W8 K0**, median T_engine 17,156.5 ms.

| Full setting | Median T_engine | MAD | Paired bootstrap improvement CI vs W1 K0 |
|---|---:|---:|---|
| W1 K0 | 22,099.2 ms | 4,715.55 ms | baseline |
| W4 K0 | 17,844.2 ms | 55.58 ms | -2.33% .. 33.25% |
| W8 K0 | 17,156.5 ms | 353.81 ms | -0.73% .. 37.34% |
| W8 K4 | 17,611.2 ms | 429.66 ms | -3.78% .. 35.93% |

These full-run confidence intervals include zero. The selection is an empirical, preliminary median choice, not a statistically established win over W1. Candidates within 3% of the fastest median prefer smaller W then K. Full tuning prioritizes baseline, a matching earlier winner and representative parallel/close settings before the remaining Cartesian product. It stops between complete blocks; a slow block can exceed the nominal budget. More settings/repetitions would cost more user time.

T_engine starts after mapping/parsing and ends after close drain; T_call includes mapping/parsing but excludes process startup. T_user above includes process startup and final reporting. AutoTune read-back CRC/count verification is outside these timers. CPU times sum process kernel/user time; lifetime peak working set is cumulative, not an isolated per-trial allocation peak. Queue-depth and close-wait diagnostics are in the JSONL files. No T_durable measurement is claimed.

The shipped profile is limited to ZIP small-file workloads, this version and an exact environment fingerprint. Other archives use defaults. 7z/RAR use the official handler's strategy and have no separate learned profile. Batch sizes, streaming thresholds and W/K can be overridden manually; exhaustive batch/threshold, zlib-ng, IOCP, prefetch and decoder-comparison studies remain future experiments. Their benefits are not assumed.

CPU: Ryzen 3 4100, 4 cores/8 logical processors; physical RAM 16,957,935,616 bytes. NTFS volume total 510,976,323,584 bytes; free during full tuning 43,643,596,800 bytes. Windows build 26200.9711. AutoTune's legacy registry product label says Windows 10 Pro even though the host reports Windows 11; build number is used in the fingerprint. Defender status was unobservable to the tuner and was preserved as unknown. A separate post-measurement read-only Get-MpComputerStatus query confirmed antivirus and real-time protection enabled; protection was never disabled. Exact recorded environment and raw summaries are in logs/autotune/ and logs/environment.json.
