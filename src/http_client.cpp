#include "http_client.h"
#include <curl/curl.h>
#include <iostream>
#include "utils.h"
#include <mutex>
#include <iomanip>

// 线程安全的CURL初始化和清理
std::mutex curlMutex;  // 用于保证CURL全局初始化的线程安全
bool curlInitialized = false;

// 回调函数，用于处理 CURL 响应数据
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch(std::bad_alloc &e) {
        return 0;
    }
    return newLength;
}

// 确保CURL全局只初始化一次
void initCurlOnce() {
    std::lock_guard<std::mutex> lock(curlMutex);
    if (!curlInitialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curlInitialized = true;
    }
}

// 发送 HTTP 请求并返回响应，兼容原来的单线程调用，也能在多线程中使用
std::string sendHttpRequest(const std::string& url) {
    initCurlOnce();  // 确保全局CURL只初始化一次

    CURL* curl = curl_easy_init();  // 每个线程独立使用自己的CURL句柄
    CURLcode res;
    std::string response;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // 自动处理重定向
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);        // 设置超时时间

        // 使用共享连接池来减少连接开销
        curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);   // 启用TCP Keep-Alive

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            log(LogLevel::LOGERROR, "curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)) + " URL: " + url);
        } 
        // else {
        //     log(LogLevel::INFO,"API Response: " + std::string(response) );
        // }
        curl_easy_cleanup(curl);  // 清理当前线程的CURL句柄
    } else {
        log(LogLevel::LOGERROR, "Failed to initialize CURL.");
    }

    return response;
}

// 构建 Telegram 发送消息的 URL 并对文本参数进行编码
std::string buildTelegramUrl(const std::string& text) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : text) {
        if (c == ' ') {
            escaped << "%20";  // 空格
        } else if (c == '\n') {
            escaped << "%0A";  // 换行符
        } else {
            escaped << c;  // 其他字符保持原样
        }
    }
    return escaped.str();
}

std::string escapeTelegramUrl(const std::string& text) {
    std::ostringstream escapedText;

    for (size_t i = 0; i < text.size(); ++i) {
        // 转义 MarkdownV2 中的特殊字符：!、[、]、(、)、.
        if (text[i] == '!' || text[i] == '[' || text[i] == ']' || text[i] == '(' || text[i] == ')' || text[i] == '.') {
            escapedText << '\\';  // 在这些字符前添加反斜杠进行转义
        }

        // 保留其他字符，不转义 ** 和其他符号
        escapedText << text[i];
    }

    return escapedText.str();
}


