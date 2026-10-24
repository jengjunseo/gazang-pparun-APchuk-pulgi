// HYPER EXTRACT — PathGuard (TICKET-004, owner: Opus 5.5, reviewer: GPT-6)
//
// Turns decoded entry names into SafePaths BEFORE any parallel work (v0.3 §1-5, §2.3):
//  * rejects: "..", absolute / UNC / \\?\ / drive paths, ':' (ADS), over-long names, too deep
//  * sanitizes: control chars and <>"|?* -> '_', trailing dot/space -> '_', reserved device
//    names (CON, NUL, COM1, LPT¹, ...) -> '_' prefix
//  * resolves case-insensitive collisions deterministically in entry-index order:
//    later entry becomes "name (2).ext", "(3)", ... ; file-vs-directory conflicts too.
//    CollisionPolicy::Fail rejects instead.
//  * symlink entries are skipped (never created)
//  * records original_entry_index -> final_relative_path for the manifest
//
// String checks alone do NOT make extraction safe (TOCTOU): the output root must be a
// fresh folder created and held open by HX (see RootGuard in the sink module).
// perf-claim: none
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dirplan.h"
#include "status.h"
#include "types.h"

namespace hx {

enum class CollisionPolicy { Rename, Fail };

struct GuardPolicy {
    CollisionPolicy collision = CollisionPolicy::Rename;
    uint32_t max_entries = 2'000'000;
    size_t max_component = 255;   // UTF-16 code units (NTFS limit)
    size_t max_rel_path = 30000;  // leaves room for root under the 32767 \\?\ limit
    size_t max_depth = 256;
};

enum class PathVerdict { Ok, Sanitized, Renamed, Skipped, Rejected };
const char* path_verdict_name(PathVerdict v);

struct PlannedEntry {
    uint32_t entry_index = 0;
    PathVerdict verdict = PathVerdict::Ok;
    bool is_dir = false;
    SafePath path;            // valid unless Skipped/Rejected
    std::wstring final_rel;   // final relative path ("" for skipped/rejected)
    std::string reason;       // ASCII reason code(s), never file contents
};

struct PathPlan {
    std::vector<PlannedEntry> entries;  // sorted by entry_index
    size_t n_ok = 0, n_sanitized = 0, n_renamed = 0, n_skipped = 0, n_rejected = 0;
};

// Plans all entries. Returns non-OK only for global failures (e.g. too many entries);
// individual unsafe entries are reported as Rejected inside `out`.
Status plan_paths(const std::vector<EntryMeta>& entries, const GuardPolicy& policy,
                  DirPlanner& dirs, PathPlan& out);

// Exposed for unit tests.
std::wstring fold_case(std::wstring_view s);
bool is_reserved_device_name(std::wstring_view component);
// Returns sanitized component; sets `changed` if anything was altered.
std::wstring sanitize_component(std::wstring_view comp, bool& changed);
std::wstring with_counter(std::wstring_view name, uint32_t n, bool is_dir);

}  // namespace hx
