#ifndef UTILS_H
#define UTILS_H

#include <string>

enum class LogLevel {
    INFO,
    WARNING,
    ERROR
};

bool isValidUrl(const std::string& url);
void log(LogLevel level, const std::string& message);

#endif

