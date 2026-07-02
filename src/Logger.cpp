#include "Logger.h"
#include <iostream>

LogLevel Logger::current_level = LogLevel::NONE;

static const char* level_tag(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::WARNING: return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::PANIC: return "PANIC";
        default: return "?????";
    }
}

void Logger::log(LogLevel lvl, const char* component, const std::string& msg) {
    if (lvl < current_level) return;
    std::cout << "[" << level_tag(lvl) << "] [" << component << "] " << msg << "\n";
}

void LOG_DEBUG(const char* c, const std::string& m) { Logger::log(LogLevel::DEBUG, c, m); }
void LOG_WARN (const char* c, const std::string& m) { Logger::log(LogLevel::WARNING, c, m); }
void LOG_ERROR(const char* c, const std::string& m) { Logger::log(LogLevel::ERROR, c, m); }
void LOG_PANIC(const char* c, const std::string& m) { Logger::log(LogLevel::PANIC, c, m); }
