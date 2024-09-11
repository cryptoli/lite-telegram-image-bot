#include "utils.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <zlib.h>
#include <vector>
#include <algorithm>
#include <mutex>
#include <openssl/evp.h>

std::mutex logMutex;  // 用于保护日志写入的全局互斥锁

// Base62 字符表
const std::string BASE62_ALPHABET = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
const int BASE = 62;

// 将 logLevel 转换为字符串
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

// 获取当前时间的字符串表示
std::string getCurrentTime() {
    std::time_t now = std::time(nullptr);
    std::tm buf;

#ifdef _WIN32
    localtime_s(&buf, &now);  // Windows 使用 localtime_s
#else
    localtime_r(&now, &buf);  // Linux 使用 localtime_r
#endif

    std::ostringstream oss;
    oss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 日志记录函数，带有线程安全保护
void log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> guard(logMutex);  // 使用互斥锁保护日志写入

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

// 计算字符串的 SHA256 哈希值
std::string calculateSHA256(const std::string& input) {
    unsigned char hash[EVP_MAX_MD_SIZE];  // EVP_MAX_MD_SIZE 是哈希摘要的最大长度
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();  // 创建 EVP 上下文
    if (mdctx == nullptr) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    // 初始化 EVP 上下文，选择 SHA256 算法
    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(mdctx);  // 释放上下文
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    // 更新上下文，输入数据
    if (EVP_DigestUpdate(mdctx, input.c_str(), input.size()) != 1) {
        EVP_MD_CTX_free(mdctx);  // 释放上下文
        throw std::runtime_error("EVP_DigestUpdate failed");
    }

    // 获取最终的哈希值
    unsigned int lengthOfHash = 0;
    if (EVP_DigestFinal_ex(mdctx, hash, &lengthOfHash) != 1) {
        EVP_MD_CTX_free(mdctx);  // 释放上下文
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    EVP_MD_CTX_free(mdctx);  // 释放上下文

    return std::string(reinterpret_cast<char*>(hash), lengthOfHash);  // 返回哈希值
}

// 将哈希值转换为 Base62 编码
std::string encodeBase62(const std::string& input) {
    std::string encoded;
    uint64_t number = 0;

    // 将输入的哈希值转换为大整数
    for (unsigned char c : input) {
        number = (number << 8) | c;
    }

    while (number > 0) {
        encoded.push_back(BASE62_ALPHABET[number % BASE]);
        number /= BASE;
    }

    std::reverse(encoded.begin(), encoded.end());
    return encoded;
}

std::string generateShortLink(const std::string& fileId) {
    std::string hash = calculateSHA256(fileId);

    return encodeBase62(hash).substr(0, 6);  // 取前 6 个字符
}