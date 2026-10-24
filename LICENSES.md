# Third-party notices and source availability

This delivery contains matching source and the following components. Preserve their license and copyright notices when redistributing. The original supplied project did not include a project-wide license grant; this delivery does not invent a license for that original code. The repository owner must choose the project-specific redistribution terms. Third-party licenses apply independently.

| Component | Version / use | License notices |
|---|---|---|
| libdeflate | 1.26, statically linked ZIP fast path | third_party/libdeflate126/COPYING (MIT) |
| zlib | 1.3.2, statically linked streaming decoder | third_party/zlib132/LICENSE (zlib license) |
| 7-Zip | 26.05, adjacent unmodified official 7z.dll and matching full source | third_party/7zip/DOC/License.txt; copying.txt; unRarLicense.txt |
| LLVM libc++ / libc++abi | bundled by Zig 0.17.0, static C++ runtime | third_party/notices/libcxx-LICENSE.TXT; libcxxabi-LICENSE.TXT |
| LLVM libunwind | bundled by Zig 0.17.0, exception runtime | third_party/notices/libunwind-LICENSE.TXT |
| mingw-w64 runtime | bundled by Zig 0.17.0 | third_party/notices/mingw-COPYING |
| Zig toolchain | 0.17.0, build-time download only | third_party/notices/zig-LICENSE |
| RAR regression fixtures | upstream libarchive and rarfile samples | tests/fixtures/LIBARCHIVE_COPYING; RARFILE_LICENSE; origin JSON files |

7-Zip contains LGPL code, BSD-licensed portions and unRAR code with its additional restriction. It is used only to decode RAR archives. No RAR compressor is implemented, derived or enabled. The DLL remains a separately replaceable library beside the executables; a replacement must provide the compatible official interface. Unmodified full matching source is included, including the build files and notices. See the upstream license texts for exact terms.

Full delivery ZIP expands the dependency source directories. To keep the Git repository compact, its complete matching sources are in third_party/vendor-sources.zip; CMake verifies the fixed SHA256 and expands them automatically on a clean checkout. No proprietary decoder download is required to build the project. Download URLs and upstream archive digests are in third_party/DEPENDENCIES.json; build tool URLs/digests are pinned in tools/bootstrap_windows.ps1.

The independent RAR golden generator used the signed RARLAB UnRAR 7.30 beta 1 executable. That verifier executable is not part of this delivery. Fixture SHA256 and verifier SHA256 are retained so the validation can be audited.
