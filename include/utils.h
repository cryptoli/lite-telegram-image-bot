#ifndef UTILS_H
#define UTILS_H

#include <string>

enum class LogLevel {
    INFO,
    WARNING,
    LOGERROR
};

void log(LogLevel level, const std::string& message);
std::string gzipCompress(const std::string& data);

#endif

