#pragma once
#include <string>
#include <string_view>
#include <utility>

namespace water::world
{
// Archive namespace only, independent of OS filesystem paths. Validation does
// not allocate; decoder admission can precede construction of retained names.
inline bool ValidAssetName(std::string_view name)
{
    if (name.empty() || name.size() > 1024) return false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/' || name[i] == '\\') {
            const auto part = name.substr(start, i-start);
            if (part.empty() || part == "." || part == "..") return false;
            start = i+1;
        } else {
            const auto c = static_cast<unsigned char>(name[i]);
            if (c < 32 || c > 126 || c == ':') return false;
        }
    }
    return true;
}
inline bool CanonicalAssetName(std::string_view name, std::string& output)
{
    if (!ValidAssetName(name)) return false;
    std::string candidate(name);
    for (auto& c : candidate)
        if (c == '/') c = '\\'; else if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    output = std::move(candidate);
    return true;
}
}
