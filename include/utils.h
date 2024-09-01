#ifndef UTILS_H
#define UTILS_H

#include <string>

enum class LogLevel {
    INFO,
    WARNING,
    ERROR
};

void log(LogLevel level, const std::string& message);

#endif

