#include "dirplan.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hx {

DirPlanner::DirPlanner() { nodes_.push_back(Node{0, 0, L""}); }

uint32_t DirPlanner::intern_child(uint32_t parent, std::wstring_view name) {
    std::wstring key = std::to_wstring(parent);
    key.push_back(L'\x01');
    key.append(name);
    auto it = index_.find(key);
    if (it != index_.end()) return it->second;
    uint32_t id = uint32_t(nodes_.size());
    std::wstring r = nodes_[parent].rel;
    if (!r.empty()) r.push_back(L'\\');
    r.append(name);
    nodes_.push_back(Node{parent, nodes_[parent].depth + 1, std::move(r)});
    index_.emplace(std::move(key), id);
    return id;
}

std::wstring DirPlanner::join_rel(uint32_t dir, std::wstring_view leaf) const {
    std::wstring r = nodes_[dir].rel;
    if (!leaf.empty()) {
        if (!r.empty()) r.push_back(L'\\');
        r.append(leaf);
    }
    return r;
}

std::wstring DirPlanner::abs_path(const std::wstring& abs_root, uint32_t dir,
                                  std::wstring_view leaf) const {
    std::wstring p;
    p.reserve(4 + abs_root.size() + nodes_[dir].rel.size() + leaf.size() + 2);
    p.append(L"\\\\?\\");
    p.append(abs_root);
    if (!nodes_[dir].rel.empty()) {
        p.push_back(L'\\');
        p.append(nodes_[dir].rel);
    }
    if (!leaf.empty()) {
        p.push_back(L'\\');
        p.append(leaf);
    }
    return p;
}

#ifdef _WIN32
Status DirPlanner::materialize(const std::wstring& abs_root) const {
    // ids are created parent-first, so ascending id order is a valid creation order.
    for (uint32_t id = 1; id < nodes_.size(); ++id) {
        std::wstring p = abs_path(abs_root, id, L"");
        if (!CreateDirectoryW(p.c_str(), nullptr)) {
            DWORD e = GetLastError();
            return Status::Make(Err::Win32, "CreateDirectory(planned) dir#" + std::to_string(id), e);
        }
    }
    return Status::Ok();
}

Status DirPlanner::ensure_lazy(const std::wstring& abs_root, uint32_t id) const {
    if (id == 0) return Status::Ok();
    uint32_t chain[512];
    uint32_t n = 0;
    for (uint32_t cur = id; cur != 0 && n < 512; cur = nodes_[cur].parent) chain[n++] = cur;
    for (uint32_t i = n; i-- > 0;) {
        std::wstring p = abs_path(abs_root, chain[i], L"");
        if (!CreateDirectoryW(p.c_str(), nullptr)) {
            DWORD e = GetLastError();
            if (e != ERROR_ALREADY_EXISTS)
                return Status::Make(Err::Win32, "CreateDirectory(lazy) dir#" + std::to_string(chain[i]), e);
        }
    }
    return Status::Ok();
}
#endif

}  // namespace hx
