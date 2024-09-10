#include "utils.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <zlib.h>
#include <vector>

std::string logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::LOGERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string getCurrentTime() {
    std::time_t now = std::time(nullptr);
    std::tm buf;

#ifdef _WIN32
    localtime_s(&buf, &now);  // Windows使用localtime_s
#else
    localtime_r(&now, &buf);  // Linux使用localtime_r
#endif

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

// Gzip 压缩实现
std::string gzipCompress(const std::string& data) {
    if (data.empty()) {
        return std::string();
    }

    // 定义缓冲区大小
    const size_t BUFSIZE = 128 * 1024;  // 128KB
    std::vector<char> buffer(BUFSIZE);

    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("deflateInit2 failed while compressing.");
    }

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());

    int ret;
    std::string output;

    // 压缩数据
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buffer.data());
        zs.avail_out = buffer.size();

        ret = deflate(&zs, Z_FINISH);

        if (output.size() < zs.total_out) {
            output.append(buffer.data(), zs.total_out - output.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        throw std::runtime_error("deflate failed while compressing.");
    }

    return output;
}
