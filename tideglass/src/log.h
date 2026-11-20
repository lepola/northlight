#pragma once

enum class LogLevel
{
    Error = 0,
    Info = 1,
    Debug = 2,
};

void LogOpen(const char* path);
// Cooperative contexts share one module log. The first context selects its path;
// reopening after final retirement appends so device recreation preserves evidence.
void LogRetain(const char* path);
void LogRelease();
void LogSetLevel(int level);
bool LogEnabled(LogLevel level);
void LogWrite(LogLevel level, const char* format, ...) __attribute__((format(printf, 2, 3)));

#define WATER_LOG_ERROR(...) LogWrite(LogLevel::Error, __VA_ARGS__)
#define WATER_LOG_INFO(...) LogWrite(LogLevel::Info, __VA_ARGS__)
#define WATER_LOG_DEBUG(...) LogWrite(LogLevel::Debug, __VA_ARGS__)
