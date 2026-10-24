// HYPER EXTRACT — DirPlanner (TICKET-004, owner: Opus 5.5)
// Holds every output directory as an id, decided BEFORE parallel extraction.
// Names stored here are FINAL names (already sanitized + collision-resolved by
// PathGuard). id 0 is the extraction root.
// perf-claim: none
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "status.h"

namespace hx {

class DirPlanner {
public:
    DirPlanner();

    // Returns the id of child `name` under `parent`, creating the plan entry if needed.
    // `name` must be a single, already-sanitized component.
    uint32_t intern_child(uint32_t parent, std::wstring_view name);

    uint32_t parent(uint32_t id) const { return nodes_[id].parent; }
    uint32_t depth(uint32_t id) const { return nodes_[id].depth; }
    const std::wstring& rel(uint32_t id) const { return nodes_[id].rel; }  // "" for root
    size_t size() const { return nodes_.size(); }                          // includes root

    std::wstring join_rel(uint32_t dir, std::wstring_view leaf) const;

    // abs_root must be an absolute path WITHOUT the \\?\ prefix and without trailing '\'.
    // Returned paths carry the \\?\ prefix (long-path safe).
    std::wstring abs_path(const std::wstring& abs_root, uint32_t dir, std::wstring_view leaf) const;

#ifdef _WIN32
    // IO-3 "planned": create all directories once, parents first. Root must already exist.
    // ERROR_ALREADY_EXISTS is an error here: root is a fresh folder created by HX,
    // so a pre-existing directory means outside interference.
    Status materialize(const std::wstring& abs_root) const;
    // IO-3 "lazy" (naive baseline): ensure all ancestors of `id` exist, per file.
    Status ensure_lazy(const std::wstring& abs_root, uint32_t id) const;
#endif

private:
    struct Node {
        uint32_t parent;
        uint32_t depth;
        std::wstring rel;
    };
    std::vector<Node> nodes_;
    std::unordered_map<std::wstring, uint32_t> index_;  // key: parent id + U+0001 + name
};

}  // namespace hx
