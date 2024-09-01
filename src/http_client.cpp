#include "http_client.h"
#include <curl/curl.h>
#include <iostream>
#include "utils.h"

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

// 发送 HTTP 请求并返回响应
std::string sendHttpRequest(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string response;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // 自动处理重定向
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 设置超时时间

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            log(LogLevel::ERROR, "curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)) + " URL: " + url);
        }
        curl_easy_cleanup(curl);
    } else {
        log(LogLevel::ERROR, "Failed to initialize CURL.");
    }
    return response;
}

// 构建 Telegram 发送消息的 URL 并对文本参数进行编码
std::string buildTelegramUrl(const std::string& text) {
    CURL* curl = curl_easy_init();
    std::string encoded_text;

    if (curl) {
        // 对 text 参数进行 URL 编码
        char* encoded = curl_easy_escape(curl, text.c_str(), text.length());
        if (encoded) {
            encoded_text = encoded;
            curl_free(encoded);
        } else {
            log(LogLevel::ERROR, "Error encoding text: " + text);
        }
        curl_easy_cleanup(curl);
    } else {
        log(LogLevel::ERROR, "Failed to initialize CURL for URL encoding.");
    }

    log(LogLevel::INFO, "Built Telegram URL: " + encoded_text);

    return encoded_text;
}
