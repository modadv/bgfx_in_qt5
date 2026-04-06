#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace engine_log {

inline std::mutex& logMutex()
{
    static std::mutex m;
    return m;
}

template <typename... Args>
inline void write(const char* level, const std::string& fmt, Args&&... args)
{
    std::lock_guard<std::mutex> lock(logMutex());
    std::ostringstream oss;
    oss << "[" << level << "] " << fmt;
    ((oss << " " << std::forward<Args>(args)), ...);
    std::cout << oss.str() << std::endl;
}

inline void write(const char* level, const char* fmt)
{
    std::lock_guard<std::mutex> lock(logMutex());
    std::cout << "[" << level << "] " << (fmt ? fmt : "") << std::endl;
}

template <typename... Args>
inline void info(const std::string& fmt, Args&&... args)
{
    write("INFO", fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(const std::string& fmt, Args&&... args)
{
    write("WARN", fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(const std::string& fmt, Args&&... args)
{
    write("ERROR", fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void debug(const std::string& fmt, Args&&... args)
{
    write("DEBUG", fmt, std::forward<Args>(args)...);
}

} // namespace engine_log

#define LOG_I(...) engine_log::info(__VA_ARGS__)
#define LOG_W(...) engine_log::warn(__VA_ARGS__)
#define LOG_E(...) engine_log::error(__VA_ARGS__)
#define LOG_D(...) engine_log::debug(__VA_ARGS__)
