#include "pathguard.h"

#include <algorithm>
#include <cwctype>
#include <climits>
#include <numeric>
#include <unordered_map>

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

const char* path_verdict_name(PathVerdict v) {
    switch (v) {
        case PathVerdict::Ok: return "ok";
        case PathVerdict::Sanitized: return "sanitized";
        case PathVerdict::Renamed: return "renamed";
        case PathVerdict::Skipped: return "skipped";
        case PathVerdict::Rejected: return "rejected";
    }
    return "?";
}

std::wstring fold_case(std::wstring_view s) {
    std::wstring out(s);
    if (out.empty()) return out;
#ifdef _WIN32
    // Invariant-locale simple uppercase: close to NTFS $UpCase behaviour, locale-independent.
    int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, s.data(), int(s.size()),
                          out.data(), int(out.size()), nullptr, nullptr, 0);
    if (n == int(s.size())) return out;
    out.assign(s);
#endif
    for (auto& c : out) {
        if (c >= L'a' && c <= L'z')
            c = wchar_t(c - (L'a' - L'A'));
        else if (c > 0x7F)
            c = wchar_t(std::towupper(c));
    }
    return out;
}

bool is_reserved_device_name(std::wstring_view comp) {
    // Base name = up to first '.', trailing spaces trimmed (Windows ignores them).
    size_t dot = comp.find(L'.');
    std::wstring_view base = comp.substr(0, dot);
    while (!base.empty() && base.back() == L' ') base.remove_suffix(1);
    std::wstring b = fold_case(base);
    if (b == L"CON" || b == L"PRN" || b == L"AUX" || b == L"NUL" || b == L"CONIN$" ||
        b == L"CONOUT$")
        return true;
    if (b.size() == 4 && (b.compare(0, 3, L"COM") == 0 || b.compare(0, 3, L"LPT") == 0)) {
        wchar_t d = b[3];
        if ((d >= L'0' && d <= L'9') || d == 0x00B9 || d == 0x00B2 || d == 0x00B3) return true;
    }
    return false;
}

std::wstring sanitize_component(std::wstring_view comp, bool& changed) {
    std::wstring s(comp);
    for (auto& c : s) {
        if (c < 0x20 || c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' ||
            c == L'*') {
            c = L'_';
            changed = true;
        }
    }
    // Trailing dots/spaces are silently stripped by Win32 and cause hidden collisions.
    for (size_t i = s.size(); i-- > 0;) {
        if (s[i] == L'.' || s[i] == L' ') {
            s[i] = L'_';
            changed = true;
        } else {
            break;
        }
    }
    if (is_reserved_device_name(s)) {
        s.insert(s.begin(), L'_');
        changed = true;
    }
    return s;
}

std::wstring with_counter(std::wstring_view name, uint32_t n, bool is_dir) {
    std::wstring suffix = L" (" + std::to_wstring(n) + L")";
    if (!is_dir) {
        size_t dot = name.rfind(L'.');
        if (dot != std::wstring_view::npos && dot != 0) {
            std::wstring r(name.substr(0, dot));
            r += suffix;
            r.append(name.substr(dot));
            return r;
        }
    }
    return std::wstring(name) + suffix;
}

namespace {

enum class Kind : uint8_t { File, Dir };

struct Planner {
    const GuardPolicy& pol;
    DirPlanner& dirs;
    std::unordered_map<std::wstring, Kind> taken;           // folded FINAL rel path -> kind
    std::unordered_map<std::wstring, uint32_t> dir_alias;   // folded ORIGINAL (sanitized) dir path -> dir id
    std::vector<std::wstring> dir_final_key;                // dir id -> folded final rel path

    Planner(const GuardPolicy& p, DirPlanner& d) : pol(p), dirs(d) {
        dir_final_key.resize(d.size());
    }

    std::wstring child_key(uint32_t dir, const std::wstring& folded_name) {
        const std::wstring& base = dir_final_key[dir];
        if (base.empty()) return folded_name;
        return base + L'\\' + folded_name;
    }
};

}  // namespace

Status plan_paths(const std::vector<EntryMeta>& entries, const GuardPolicy& pol, DirPlanner& dirs,
                  PathPlan& out) {
    out = PathPlan{};
    if (entries.size() > pol.max_entries)
        return Status::Make(Err::LimitExceeded, "too many entries: " + std::to_string(entries.size()));

    // Deterministic order: by archive index, independent of caller/thread order.
    std::vector<size_t> order(entries.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return entries[a].index < entries[b].index; });

    Planner P(pol, dirs);
    out.entries.reserve(entries.size());

    for (size_t oi : order) {
        const EntryMeta& e = entries[oi];
        PlannedEntry pe;
        pe.entry_index = e.index;
        pe.is_dir = e.is_dir;
        pe.path.entry_index = e.index;

        auto reject = [&](const char* why) {
            pe.verdict = PathVerdict::Rejected;
            pe.reason = why;
            pe.final_rel.clear();
            out.entries.push_back(std::move(pe));
            ++out.n_rejected;
        };

        if (e.is_symlink) {
            pe.verdict = PathVerdict::Skipped;
            pe.reason = "symlink_not_created";
            out.entries.push_back(std::move(pe));
            ++out.n_skipped;
            continue;
        }

        std::wstring name = e.name;
        if(name.find(L'\0')!=std::wstring::npos) { reject("nul_name"); continue; }
#if WCHAR_MAX <= 0xFFFF
        bool invalid_utf16=false;
        for(size_t j=0;j<name.size();++j){unsigned c=name[j];
            if(c>=0xD800&&c<=0xDBFF){if(j+1>=name.size()||name[j+1]<0xDC00||name[j+1]>0xDFFF){invalid_utf16=true;break;}++j;}
            else if(c>=0xDC00&&c<=0xDFFF){invalid_utf16=true;break;}
        }
        if(invalid_utf16){reject("invalid_utf16");continue;}
#endif
        std::replace(name.begin(), name.end(), L'/', L'\\');
        if (name.empty()) { reject("empty_name"); continue; }
        if (name[0] == L'\\') { reject("absolute_or_unc"); continue; }
        if (name.size() >= 2 && name[1] == L':') { reject("drive_path"); continue; }
        if (name.find(L':') != std::wstring::npos) { reject("colon_or_ads"); continue; }

        // split
        std::vector<std::wstring> comps;
        bool dotdot = false;
        size_t start = 0;
        while (start <= name.size()) {
            size_t end = name.find(L'\\', start);
            if (end == std::wstring::npos) end = name.size();
            std::wstring c = name.substr(start, end - start);
            if (c == L"..") dotdot = true;
            else if (!c.empty() && c != L".") comps.push_back(std::move(c));
            start = end + 1;
        }
        if (dotdot) { reject("dot_dot"); continue; }
        if (comps.empty()) {
            if (e.is_dir) {
                pe.verdict = PathVerdict::Skipped;
                pe.reason = "root_dir_entry";
                out.entries.push_back(std::move(pe));
                ++out.n_skipped;
            } else {
                reject("empty_after_normalize");
            }
            continue;
        }
        if (comps.size() > pol.max_depth) { reject("too_deep"); continue; }

        bool sanitized = false, too_long = false;
        size_t total_len = 0;
        for (auto& c : comps) {
            c = sanitize_component(c, sanitized);
            if (c.size() > pol.max_component) too_long = true;
            total_len += c.size() + 1;
        }
        if (too_long) { reject("component_too_long"); continue; }
        if (total_len > pol.max_rel_path) { reject("path_too_long"); continue; }

        // --- directories ---
        bool renamed = false, collision_fail = false;
        uint32_t cur = 0;
        std::wstring orig_key;
        size_t ndirs = e.is_dir ? comps.size() : comps.size() - 1;
        for (size_t i = 0; i < ndirs; ++i) {
            std::wstring fc = fold_case(comps[i]);
            orig_key = orig_key.empty() ? fc : orig_key + L'\\' + fc;
            auto al = P.dir_alias.find(orig_key);
            if (al != P.dir_alias.end()) { cur = al->second; continue; }

            std::wstring final_name = comps[i];
            std::wstring fkey = P.child_key(cur, fold_case(final_name));
            uint32_t n = 2;
            while (P.taken.count(fkey)) {  // taken by a file, or by a DIFFERENT original dir
                if (pol.collision == CollisionPolicy::Fail || n > 100000) { collision_fail = true; break; }
                final_name = with_counter(comps[i], n++, true);
                fkey = P.child_key(cur, fold_case(final_name));
                renamed = true;
            }
            if (collision_fail) break;
            if(final_name.size()>pol.max_component) { collision_fail=true; break; }
            P.taken.emplace(fkey, Kind::Dir);
            uint32_t id = dirs.intern_child(cur, final_name);
            if (P.dir_final_key.size() <= id) P.dir_final_key.resize(id + 1);
            P.dir_final_key[id] = fkey;
            P.dir_alias.emplace(orig_key, id);
            cur = id;
        }
        if (collision_fail) { reject("collision"); continue; }

        pe.path.dir_id = cur;
        if (!e.is_dir) {
            const std::wstring& leaf0 = comps.back();
            std::wstring leaf = leaf0;
            std::wstring fkey = P.child_key(cur, fold_case(leaf));
            uint32_t n = 2;
            while (P.taken.count(fkey)) {
                if (pol.collision == CollisionPolicy::Fail || n > 100000) { collision_fail = true; break; }
                leaf = with_counter(leaf0, n++, false);
                fkey = P.child_key(cur, fold_case(leaf));
                renamed = true;
            }
            if (collision_fail) { reject("collision"); continue; }
            if (leaf.size() > pol.max_component) { reject("component_too_long"); continue; }
            P.taken.emplace(fkey, Kind::File);
            pe.path.leaf = leaf;
        }
        pe.final_rel = dirs.join_rel(cur, pe.path.leaf);
        if(pe.final_rel.size()>pol.max_rel_path) { reject("renamed_path_too_long"); continue; }
        if (renamed) {
            pe.verdict = PathVerdict::Renamed;
            pe.reason = sanitized ? "sanitized,renamed" : "renamed";
            ++out.n_renamed;
        } else if (sanitized) {
            pe.verdict = PathVerdict::Sanitized;
            pe.reason = "sanitized";
            ++out.n_sanitized;
        } else {
            pe.verdict = PathVerdict::Ok;
            ++out.n_ok;
        }
        out.entries.push_back(std::move(pe));
    }
    return Status::Ok();
}

}  // namespace hx
