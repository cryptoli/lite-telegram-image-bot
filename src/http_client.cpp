#include "http_client.h"
#include <curl/curl.h>
#include <iostream>

// 用于处理 CURL 回调，将响应内容写入字符串
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
        std::cout << "Sending request to URL: " << url << std::endl;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            std::cout << "Request successful!" << std::endl;
        }
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Failed to initialize CURL." << std::endl;
    }
    return response;
}

// 构建 Telegram 发送消息的 URL 并对文本参数进行编码
std::string buildTelegramUrl(const std::string& apiToken, const std::string& chatId, const std::string& text) {
    CURL* curl = curl_easy_init();
    std::string encoded_text;

    if (curl) {
        char* encoded = curl_easy_escape(curl, text.c_str(), text.length());
        if (encoded) {
            encoded_text = encoded;
            curl_free(encoded);
        } else {
            std::cerr << "Error encoding text: " << text << std::endl;
        }
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Failed to initialize CURL for URL encoding." << std::endl;
    }

    std::string url = "https://api.telegram.org/bot" + apiToken + "/sendMessage?chat_id=" + chatId + "&text=" + encoded_text;
    std::cout << "Built Telegram URL: " << url << std::endl;

    return url;
}
