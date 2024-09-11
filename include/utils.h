#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <cstdint>

enum class LogLevel {
    INFO,
    WARNING,
    LOGERROR
};

// 工具函数声明
std::string logLevelToString(LogLevel level);
std::string getCurrentTime();
void log(LogLevel level, const std::string& message);
std::string gzipCompress(const std::string& data);

// 短链生成函数声明
std::string generateShortLink(const std::string& fileId);

#endif

