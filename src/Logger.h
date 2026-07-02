#pragma once
#include <string>

enum class LogLevel { DEBUG = 0, WARNING = 1, ERROR = 2, PANIC = 3, NONE = 4 };

class Logger {
public:
    static LogLevel current_level;
    static void set_level(LogLevel lvl) { current_level = lvl; }
    static bool enabled() { return current_level != LogLevel::NONE; }
    static void log(LogLevel lvl, const char* component, const std::string& msg);
};

void LOG_DEBUG(const char* comp, const std::string& msg);
void LOG_WARN (const char* comp, const std::string& msg);
void LOG_ERROR(const char* comp, const std::string& msg);
void LOG_PANIC(const char* comp, const std::string& msg);
