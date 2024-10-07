#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <fstream>
#include <sstream>

extern std::mutex logMutex;
extern std::ofstream logFile;

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

template<typename... Args>
std::string formatMessage(LogLevel level, const std::string& message, Args&&... args) {
    std::ostringstream oss;
    oss << "[" << getCurrentTime() << "] [" << logLevelToString(level) << "] " << message;

    (void)std::initializer_list<int>{(oss << " " << std::forward<Args>(args), 0)...};

    return oss.str();
}

template<typename... Args>
void log(LogLevel level, const std::string& message, Args&&... args) {
    std::lock_guard<std::mutex> guard(logMutex);

    std::string formattedMessage = formatMessage(level, message, std::forward<Args>(args)...);

    if (logFile.is_open()) {
        logFile.write(formattedMessage.c_str(), formattedMessage.length());
        logFile.put('\n');
    } else {
        std::cerr << "Unable to open log file!" << std::endl;
    }

    std::cout << formattedMessage << std::endl;
}

#endif

