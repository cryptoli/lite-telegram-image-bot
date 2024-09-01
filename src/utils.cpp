#include "utils.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>

std::string logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string getCurrentTime() {
    std::time_t now = std::time(nullptr);
    std::tm buf;
    localtime_r(&now, &buf);
    std::ostringstream oss;
    oss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void log(LogLevel level, const std::string& message) {
    std::string timestamp = getCurrentTime();
    std::string levelStr = logLevelToString(level);

    std::string formattedMessage = "[" + timestamp + "] [" + levelStr + "] " + message;

    // 输出到日志文件
    std::ofstream logFile("bot.log", std::ios_base::app);
    if (logFile.is_open()) {
        logFile << formattedMessage << std::endl;
        logFile.close();
    } else {
        std::cerr << "Unable to open log file!" << std::endl;
    }

    // 同时输出到控制台
    std::cout << formattedMessage << std::endl;
}
