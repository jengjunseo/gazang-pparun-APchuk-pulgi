# HYPER EXTRACT ZIP AutoTune

- Dataset: synthetic 10104 files, 286994987 bytes
- Windows native QPC, fresh output root, all handles closed
- CRC + file count verified after timer
- Decision: median within 3% of fastest, lowest W then K
- Confidence: preliminary; 2 paired randomized blocks
- T_engine excludes map/parse and verification. T_call includes map/parse, excludes process startup and read-back verification.
- Warm uncontrolled OS cache; process memory peak is cumulative for this process, not per trial.

| Workers | Closers | Median ms | p90 | MAD | 95% paired bootstrap improvement vs W1K0 |
|---:|---:|---:|---:|---:|---|
| 1 | 0 | 22099.2 | 25871.7 | 4715.55 | 0 .. 0 |
| 4 | 0 | 17844.2 | 17888.6 | 55.5779 | -0.0232924 .. 0.332467 |
| 8 | 0 | 17156.5 | 17439.6 | 353.813 | -0.00728817 .. 0.373377 |
| 8 | 4 | 17611.2 | 17954.9 | 429.66 | -0.0378059 .. 0.35925 |

Fastest: W8 K0 (17156.5 ms). Chosen: W8 K0.
