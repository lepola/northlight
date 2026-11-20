#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <atomic>
#include <mutex>

namespace
{
FILE* g_file = nullptr;
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
std::mutex g_lock;
unsigned g_users = 0;

const char* LevelName(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Error:
        return "error";
    case LogLevel::Info:
        return "info";
    default:
        return "debug";
    }
}
}

void LogOpen(const char* path)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_file)
        fclose(g_file);
    g_file = path ? fopen(path, "w") : nullptr;
}

void LogRetain(const char* path)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_users++ && !g_file) g_file = path ? fopen(path, "a") : nullptr;
}

void LogRelease()
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_users && !--g_users) {
        if (g_file) fclose(g_file);
        g_file = nullptr;
    }
}

void LogSetLevel(int level)
{
    g_level = level;
}

bool LogEnabled(LogLevel level)
{
    std::lock_guard<std::mutex> lock(g_lock);
    return g_file && static_cast<int>(level) <= g_level;
}

void LogWrite(LogLevel level, const char* format, ...)
{
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_file || static_cast<int>(level) > g_level)
        return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(g_file, "%02u:%02u:%02u.%03u [%s] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            LevelName(level));
    va_list args;
    va_start(args, format);
    vfprintf(g_file, format, args);
    va_end(args);
    fputc('\n', g_file);
    fflush(g_file);
}
