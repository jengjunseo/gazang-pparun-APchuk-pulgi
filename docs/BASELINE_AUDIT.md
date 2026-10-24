# Baseline audit (2026-10-24)
- Input: supplied nested HYPER_EXTRACT_v0.6_SOURCE.zip. No repository metadata or vendored library sources.
- Original CTest groups: core, pathguard_prop, zip, zip_mutation, zip_writer. Original Windows selftest lives in separate hx-tune.
- No MSVC/CMake/Ninja/Zig on PATH; no Visual Studio, LLVM, CMake or MSYS at standard install paths. Python from Codex bundled runtime available.
- Existing bootstrap pins Zig 0.17.0; verified its version against official download index before use. CMake/Ninja official release API digests checked by provisioning.
- Risks confirmed by code inspection: missing local CRC/size and compressed-region overlap checks; fast decoder ignores unused compressed tail; paths sanitize embedded NUL; nontransactional output; profile uses substring parsing without environment fingerprint; partial output retained; no external format engine or GUI.
- No Windows baseline build or test result is assumed from previous Linux reports.
- Initial sandbox network DNS unavailable; official tool download runs through approved network escalation. CIM/Defender reads unavailable in sandbox; retry read-only outside sandbox.
- No project license grant present; preserve supplied notices and do not invent upstream ownership or license.
