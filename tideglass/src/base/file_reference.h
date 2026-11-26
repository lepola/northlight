#pragma once
#include <string>

// A "./" reference travels with its settings file or archive list. Existing absolute
// Windows references retain their meaning in both the standalone and cooperative hosts.
inline bool ResolveFileReference(const std::wstring& containingFile, std::wstring& value)
{
    if (value.compare(0, 2, L"./") && value.compare(0, 2, L".\\")) return true;
    const auto slash = containingFile.find_last_of(L"/\\");
    if (slash == std::wstring::npos || value.find(L':') != std::wstring::npos) return false;
    std::size_t start = 2;
    while (start <= value.size()) {
        const auto end = value.find_first_of(L"/\\", start);
        const auto component = value.substr(start, end - start);
        if (component.empty() || component == L"." || component == L"..") return false;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    value = containingFile.substr(0, slash + 1) + value.substr(2);
    return value.size() < 32768;
}
