#pragma once
#include "host_client.h"
#include <string>
#include <vector>

class WaterHostSettings
{
public:
    bool ConfigureModule(HMODULE module)
    {
        std::vector<wchar_t> path(512);
        for (;;) {
            const DWORD count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (!count) return false;
            if (count < path.size() - 1) {
                std::wstring file(path.data(), count);
                const auto slash = file.find_last_of(L"/\\");
                return slash != std::wstring::npos && ConfigureRoot(file.substr(0, slash + 1));
            }
            if (path.size() >= 32768) return false;
            path.resize(path.size() * 2);
        }
    }
    bool ConfigureRoot(std::wstring root)
    {
        if (root.empty()) return false;
        if (root.back() != L'\\' && root.back() != L'/') root += L'\\';
        m_root = std::move(root);
        const std::wstring settings = m_root + L"water.ini";
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, settings.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1) return false;
        std::string utf8(static_cast<size_t>(length), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, settings.c_str(), -1, utf8.data(), length, nullptr, nullptr)) return false;
        utf8.pop_back();
        m_settings = std::move(utf8);
        m_enabled = GetPrivateProfileIntW(L"WaterIntegration", L"Enable", 0, (m_root + L"water-host.ini").c_str()) != 0;
        m_compositeAfterEffects = GetPrivateProfileIntW(L"WaterIntegration", L"CompositeAfterEffects", 0,
                                                       (m_root + L"water-host.ini").c_str()) != 0;
        return true;
    }
    bool Open()
    {
        if (!m_enabled) return false;
        return m_module.API() || m_module.Open((m_root + L"water-core.dll").c_str());
    }
    WaterHostModule& Module() { return m_module; }
    const std::string& SettingsPath() const { return m_settings; }
    bool Enabled() const { return m_enabled; }
    bool CompositeAfterEffects() const { return m_compositeAfterEffects; }
private:
    WaterHostModule m_module;
    std::wstring m_root;
    std::string m_settings;
    bool m_enabled = false;
    bool m_compositeAfterEffects = false;
};
