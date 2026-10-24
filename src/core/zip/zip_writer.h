// HYPER EXTRACT - deterministic small/medium ZIP writer for golden tests/AutoTune.
// This is NOT a general-purpose production archiver: non-ZIP64 (<4GiB) output only.
#pragma once
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include "../status.h"
namespace hx::zip {
struct WriteItem {
    std::string utf8_name; // relative UTF-8 path, '/' separator; no NUL
    std::span<const std::byte> data;
    bool deflate = true;
};
Status make_test_zip(const std::filesystem::path& dest, std::span<const WriteItem> entries);
}
