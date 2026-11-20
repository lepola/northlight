#pragma once

#include <windows.h>
#include "file_reference.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// One parse attempt. Missing keys use product defaults; malformed present values
// reject the complete attempt. ConfigFile owns publication and warning lifetime.
class ConfigReader
{
public:
    explicit ConfigReader(const std::string& path) : m_path(path) {}
    const std::vector<std::string>& Errors() const { return m_errors; }
    void Reject(const char* key) {
        if (std::find(m_errors.begin(), m_errors.end(), key) == m_errors.end()) m_errors.emplace_back(key);
    }

    template<class Number> Number NumberValue(const char* key, Number fallback) {
        char value[96] = {};
        if (!Read(key, value)) return fallback;
        const char* cursor = value;
        Number parsed;
        if (!NumberToken(cursor, parsed) || *Whitespace(cursor)) { Reject(key); return fallback; }
        return parsed;
    }
    int Int(const char* key, int fallback) { return NumberValue(key, fallback); }
    float Float(const char* key, float fallback) { return NumberValue(key, fallback); }
    std::string PathValue(const char* key) {
        char value[MAX_PATH] = {};
        return Read(key, value) ? value : "";
    }

    void Color(const char* key, const float* fallback, float* out) {
        std::copy(fallback, fallback + 3, out);
        char value[288] = {};
        if (!Read(key, value)) return;
        const char* cursor = value;
        float parsed[3] = {};
        for (unsigned i = 0; i < 3; ++i) {
            if (!NumberToken(cursor, parsed[i])) { Reject(key); return; }
            const char* end = cursor;
            cursor = Whitespace(cursor);
            if (i < 2) {
                if (*cursor == ',') ++cursor;
                else if (cursor == end) { Reject(key); return; }
            }
        }
        if (*Whitespace(cursor)) { Reject(key); return; }
        for (unsigned i = 0; i < 3; ++i) out[i] = std::clamp(parsed[i], 0.f, 1.f);
    }

    std::shared_ptr<const std::wstring> WidePath(const char* key,const std::shared_ptr<const std::wstring>& previous) {
        char value[32768] = {};
        if (!Read(key, value) || !value[0]) return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
        if (!length) { Reject(key); return previous; }
        std::wstring decoded(length, 0);
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, decoded.data(), length)) {
            Reject(key); return previous;
        }
        decoded.resize(length - 1);
        if (decoded.compare(0, 2, L"./") == 0 || decoded.compare(0, 2, L".\\") == 0) {
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m_path.c_str(), -1, nullptr, 0);
            std::wstring settings(size, 0);
            if (!size || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m_path.c_str(), -1, settings.data(), size) ||
                !ResolveFileReference(settings, decoded)) { Reject(key); return previous; }
        }
        if (previous && *previous == decoded) return previous;
        return std::make_shared<const std::wstring>(std::move(decoded));
    }

private:
    static const char* Whitespace(const char* cursor) {
        while (std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        return cursor;
    }
    template<class Number> static bool NumberToken(const char*& cursor, Number& parsed) {
        cursor = Whitespace(cursor);
        char* end = nullptr;
        errno = 0;
        if constexpr (std::is_same_v<Number, float>) {
            parsed = std::strtof(cursor, &end);
            if (errno == ERANGE || !std::isfinite(parsed)) return false;
        } else {
            static_assert(std::is_same_v<Number, int>);
            const char* digits = (*cursor == '+' || *cursor == '-') ? cursor + 1 : cursor;
            const bool hexadecimal = digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
            const long value = std::strtol(cursor, &end, hexadecimal ? 16 : 10);
            if (errno == ERANGE || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) return false;
            parsed = static_cast<int>(value);
        }
        if (cursor == end) return false;
        cursor = end;
        return true;
    }
    template<size_t Size> bool Read(const char* key, char (&value)[Size]) {
        // Distinguish an absent key from an explicitly empty (invalid numeric) one.
        const DWORD length = GetPrivateProfileStringA("Water", key, "\x01", value, Size, m_path.c_str());
        if (length == 1 && value[0] == '\x01') return false;
        if (length >= Size - 1) { Reject(key); return false; }
        return true;
    }
    const std::string& m_path;
    std::vector<std::string> m_errors;
};
