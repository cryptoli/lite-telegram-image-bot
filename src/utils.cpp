#include "utils.h"
#include <iostream>
#include <fstream>
#include <regex>

bool isValidUrl(const std::string& url) {
    const std::regex pattern(R"(^https?:\/\/[^\s/$.?#].[^\s]*$)");
    return std::regex_match(url, pattern);
}

void log(const std::string& message) {
    std::ofstream logFile("bot.log", std::ios_base::app);
    if (logFile.is_open()) {
        std::time_t now = std::time(nullptr);
        logFile << std::ctime(&now) << ": " << message << std::endl;
        logFile.close();
    } else {
        std::cerr << "Unable to open log file!" << std::endl;
    }
}