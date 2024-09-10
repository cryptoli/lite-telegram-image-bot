#include "request_handler.h"
#include "http_client.h"
#include "utils.h"
#include "config.h"
#include "db_manager.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <curl/curl.h>
#include <algorithm>

std::string getMimeType(const std::string& filePath, const std::map<std::string, std::string>& mimeTypes, const std::string& defaultMimeType = "application/octet-stream") {
    try {
        // 查找文件扩展名
        size_t pos = filePath.find_last_of(".");
        std::string extension;
        
        if (pos != std::string::npos && pos != filePath.length() - 1) {
            extension = filePath.substr(pos);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        }
        if (extension.empty()) {
            if (filePath.find("photo") != std::string::npos) {
                return "image/jpeg";
            }
            if (filePath.find("video") != std::string::npos) {
                return "video/mp4";
            }
        }
        auto it = mimeTypes.find(extension);
        if (it != mimeTypes.end()) {
            return it->second;
        } else {
            return defaultMimeType;
        }
    } catch (const std::exception& e) {
        return defaultMimeType;
    }
}

std::string getFileExtension(const std::string& filePath) {
    std::size_t pos = filePath.find_last_of(".");
    if (pos != std::string::npos) {
        return filePath.substr(pos);
    }
    return ""; // No extension found
}

// 流式传输回调函数
size_t streamWriteCallback(void* ptr, size_t size, size_t nmemb, httplib::Response* res) {
    size_t totalSize = size * nmemb;
    res->body.append(static_cast<char*>(ptr), totalSize);
    return totalSize;
}

// 处理视频和文档的直接流式传输（不缓存）
void handleStreamRequest(const httplib::Request& req, httplib::Response& res, const std::string& fileDownloadUrl, const std::string& mimeType) {
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, fileDownloadUrl.c_str());

        // 启用 HTTP Keep-Alive
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);

        // 增加缓冲区大小
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);

        // 设置请求超时
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        // 设置分块传输
        // res.set_header("Transfer-Encoding", "chunked");

        // 设置回调函数，流式传输数据
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);

        // 执行请求
        curl_easy_perform(curl);

        // 清理 curl 资源
        curl_easy_cleanup(curl);
    }
    
    res.set_header("Content-Type", mimeType);
    res.set_header("Accept-Ranges", "bytes");  // 支持分段下载
}

void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken, const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager, CacheManager& memoryCache, const std::string& telegramApiUrl, const Config& config) {
    log(LogLevel::INFO, "Received request for image.");

    if (req.matches.size() < 2) {
        res.status = 400;
        res.set_content("Bad Request", "text/plain");
        log(LogLevel::LOGERROR, "Bad request: URL does not match expected format.");
        return;
    }

    std::string fileId = req.matches[1];

    // 验证 fileId 的合法性
    std::regex fileIdRegex("^[A-Za-z0-9_-]+$");
    if (!std::regex_match(fileId, fileIdRegex)) {
        res.status = 400;
        res.set_content("Invalid File ID", "text/plain");
        log(LogLevel::LOGERROR, "Invalid file ID received: " + fileId);
        return;
    }

    log(LogLevel::INFO, "Checking file path from memory cache for file ID: " + fileId);

    // Step 1: 先从 memoryCache 中获取 filePath
    std::string cachedFilePath;
    bool isMemoryCacheHit = memoryCache.getFilePathCache(fileId, cachedFilePath);

    if (!isMemoryCacheHit) {
        log(LogLevel::INFO, "Memory cache miss. Requesting file information from Telegram for file ID: " + fileId);

        // 如果 memoryCache 中没有 filePath，调用 getFile 接口获取文件路径
        std::string telegramFileUrl = telegramApiUrl + "/bot" + apiToken + "/getFile?file_id=" + fileId;
        std::string fileResponse = sendHttpRequest(telegramFileUrl);

        if (fileResponse.empty()) {
            res.status = 500;
            res.set_content("Failed to get file information from Telegram", "text/plain");
            log(LogLevel::LOGERROR, "Failed to retrieve file information from Telegram.");
            return;
        }

        nlohmann::json jsonResponse = nlohmann::json::parse(fileResponse);
        if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
            std::string filePath = jsonResponse["result"]["file_path"];
            log(LogLevel::INFO, "Retrieved file path: " + filePath);

            // 将 filePath 缓存
            memoryCache.addFilePathCache(fileId, filePath, 3600);

            // 获取文件扩展名
            std::string extension = getFileExtension(filePath);

            // Step 2: 根据 filePath 从 Telegram 下载文件并缓存
            std::string telegramFileDownloadUrl = telegramApiUrl + "/file/bot" + apiToken + "/" + filePath;
            std::string fileData = sendHttpRequest(telegramFileDownloadUrl);

            if (fileData.empty()) {
                res.status = 500;
                res.set_content("Failed to download file from Telegram", "text/plain");
                log(LogLevel::LOGERROR, "Failed to download file from Telegram for file path: " + filePath);
                return;
            }

            // 将下载的文件存入 image 缓存
            cacheManager.cacheImage(fileId, fileData, extension);

            // 返回文件
            std::string mimeType = getMimeType(filePath, mimeTypes);
            res.set_content(fileData, mimeType);
            log(LogLevel::INFO, "Successfully served and cached file for file ID: " + fileId);
        } else {
            res.status = 404;
            res.set_content("File Not Found", "text/plain");
            log(LogLevel::LOGERROR, "File not found in Telegram for ID: " + fileId);
            return;
        }
    } else {
        log(LogLevel::INFO, "File path cache hit for file ID: " + fileId);

        // 如果缓存命中，使用缓存的 filePath 进行文件下载
        std::string extension = getFileExtension(cachedFilePath);

        std::string telegramFileDownloadUrl = telegramApiUrl + "/file/bot" + apiToken + "/" + cachedFilePath;
        std::string fileData = sendHttpRequest(telegramFileDownloadUrl);

        if (fileData.empty()) {
            res.status = 500;
            res.set_content("Failed to download file from Telegram", "text/plain");
            log(LogLevel::LOGERROR, "Failed to download file from Telegram for cached file path: " + cachedFilePath);
            return;
        }

        // 将下载的文件存入缓存
        cacheManager.cacheImage(fileId, fileData, extension);

        // 返回文件
        std::string mimeType = getMimeType(cachedFilePath, mimeTypes);
        res.set_content(fileData, mimeType);
        log(LogLevel::INFO, "Successfully served cached file for file ID: " + fileId);
    }
}

std::string getBaseUrl(const std::string& url) {
    std::regex urlRegex(R"((https?:\/\/[^\/:]+(:\d+)?))");
    std::smatch match;
    if (std::regex_search(url, match, urlRegex)) {
        return match.str(0);
    }
    return "";
}