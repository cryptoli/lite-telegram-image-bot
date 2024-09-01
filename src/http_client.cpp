#include "http_client.h"
#include <curl/curl.h>
#include <iostream>

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch(std::bad_alloc &e) {
        return 0;
    }
    return newLength;
}

std::string sendHttpRequest(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string response;

    curl = curl_easy_init();
    if (curl) {
        // 使用 curl_easy_escape 编码 URL
        char* encoded_url = curl_easy_escape(curl, url.c_str(), url.length());
        if (encoded_url) {
            std::cout << "Sending request to URL: " << encoded_url << std::endl;

            curl_easy_setopt(curl, CURLOPT_URL, encoded_url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            }

            curl_free(encoded_url); // 释放编码后的 URL
        } else {
            std::cerr << "Failed to encode URL." << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return response;
}
