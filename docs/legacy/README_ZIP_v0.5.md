# HYPER EXTRACT ZIP prototype (GPT-6 v0.5)

**Source only: there is no prebuilt Windows hx.exe in this package.**

`hx-tune.exe` in the Opus package is a separate write-path tuner.
This ZIP engine supports Store(0), raw DEFLATE(8), central-directory ZIP64,
UTF-8/CP949/CP437 names, data descriptors, CRC32 verification and safe-path
planning. It rejects unsupported/encrypted/multipart ZIP files.

## Build on a Windows development machine

Install Visual Studio C++ Build Tools and CMake, then run:

```
cmake --preset msvc-release
cmake --build --preset msvc-release
build\msvc\Release\hx.exe x C:\archive.zip -o C:\new_output_directory
```

The output directory must NOT exist yet; its parent must exist.
`--workers N --closers N --codepage 949|437|65001` are optional.

CMake searches for libdeflate and zlib and uses FetchContent for pinned
upstream release tags if missing (first configure may require internet).
For Windows static self-contained release, review linkage and DLL imports.

## Tested here

On Linux/GCC: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4 && ctest --test-dir build --output-on-failure`

4/4 test targets passed, including deterministic 25,000-input ZIP mutation
smoke test; ASan+UBSan passed 4/4. Windows runtime and performance NOT tested.

Integration requests and limitations are in
`HYPER_EXTRACT_GPT6_ZIP_엔진_인수인계_v0.5.md`.
