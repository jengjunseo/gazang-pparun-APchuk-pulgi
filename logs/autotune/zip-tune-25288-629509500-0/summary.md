# HYPER EXTRACT ZIP AutoTune

- Dataset: synthetic 512 files, 11999742 bytes
- Windows native QPC, fresh output root, all handles closed
- CRC + file count verified after timer
- Decision: median within 3% of fastest, lowest W then K
- Confidence: preliminary; 3 paired randomized blocks
- T_engine excludes map/parse and verification. T_call includes map/parse, excludes process startup and read-back verification.
- Warm uncontrolled OS cache; process memory peak is cumulative for this process, not per trial.

| Workers | Closers | Median ms | p90 | MAD | 95% paired bootstrap improvement vs W1K0 |
|---:|---:|---:|---:|---:|---|
| 1 | 0 | 2335.7 | 2625.25 | 361.942 | 0 .. 0 |
| 1 | 4 | 2536.06 | 2559.85 | 29.7423 | -1.94411 .. 0.0598985 |
| 2 | 0 | 1829.1 | 2114.57 | 32.6601 | -1.09879 .. 0.334069 |
| 2 | 4 | 1931.75 | 2227.42 | 15.9785 | -1.19824 .. 0.172948 |
| 4 | 0 | 1953.88 | 3656.58 | 795.746 | -0.513266 .. 0.163472 |
| 4 | 4 | 2309.32 | 2447.84 | 173.148 | -1.28764 .. 0.0797637 |
| 8 | 0 | 1354.39 | 2227.77 | 302.917 | -0.554085 .. 0.610226 |
| 8 | 4 | 1260.89 | 1324.48 | 79.4828 | 0.124822 .. 0.532596 |

Fastest: W8 K4 (1260.89 ms). Chosen: W8 K4.
