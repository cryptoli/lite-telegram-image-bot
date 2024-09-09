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
        curl_easy_setopt(curl, CURLOPT_URL, fileDownloadUrl.c_str());  // 设置要请求的文件下载 URL
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);  // 设置回调函数，流式传输数据
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);  // 将数据写入响应
        curl_easy_perform(curl);  // 执行请求
        curl_easy_cleanup(curl);  // 清理 curl 资源
    }
    res.set_header("Content-Type", mimeType);  // 设置 MIME 类型
    res.set_header("Accept-Ranges", "bytes");  // 告诉浏览器支持分段下载
}

void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken, const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager, const std::string& telegramApiUrl, const Config& config) {
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

    log(LogLevel::INFO, "Checking cache for file ID: " + fileId);

    // 先从缓存中获取文件数据
    std::string extension = "";  // 用于在缓存命中时设置扩展名
    std::string cachedImageData = cacheManager.getCachedImage(fileId, extension);

    if (!cachedImageData.empty()) {
        // 如果缓存命中，直接返回缓存的数据
        std::string mimeType = getMimeType("." + extension, mimeTypes);  // 根据文件扩展名获取 MIME 类型
        res.set_content(cachedImageData, mimeType);
        log(LogLevel::INFO, "Cache hit: Served file for file ID: " + fileId + " from cache.");
        return;
    }

    // 如果缓存未命中，向 Telegram 服务器请求文件信息
    log(LogLevel::INFO, "Cache miss. Requesting file ID: " + fileId + " from Telegram.");

    std::string telegramFileUrl = telegramApiUrl + "/bot" + apiToken + "/getFile?file_id=" + fileId;
    log(LogLevel::INFO, "Request url: " + telegramFileUrl);

    std::string fileResponse = sendHttpRequest(telegramFileUrl);
    if (fileResponse.empty()) {
        res.status = 500;
        res.set_content("Failed to get file information from Telegram", "text/plain");
        log(LogLevel::LOGERROR, "Failed to retrieve file information from Telegram.");
        return;
    }

    log(LogLevel::INFO, "Received response from Telegram for file ID: " + fileId);

    try {
        nlohmann::json jsonResponse = nlohmann::json::parse(fileResponse);
        if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
            std::string filePath = jsonResponse["result"]["file_path"];
            extension = getFileExtension(filePath);  // 获取文件扩展名
            std::string fileDownloadUrl = telegramApiUrl + "/file/bot" + apiToken + "/" + filePath;
            log(LogLevel::INFO, "File path obtained: " + filePath);

            std::string mimeType = getMimeType(filePath, mimeTypes);
            std::string path = req.path;
            std::string baseUrl = getBaseUrl(config.getWebhookUrl());

            // 保存到数据库
            DBManager dbManager("bot_database.db");
            dbManager.addFile("unknown", fileId, baseUrl + path, "文件", extension);

            // 检查是否是视频或文档类型，不缓存，直接流式传输
            if (mimeType.find("video") != std::string::npos || mimeType.find("application") != std::string::npos) {
                log(LogLevel::INFO, "Streaming file directly from Telegram (no caching) for MIME type: " + mimeType);
                handleStreamRequest(req, res, fileDownloadUrl, mimeType);
                return;
            }

            // 下载文件
            std::string fileData = sendHttpRequest(fileDownloadUrl);
            if (fileData.empty()) {
                log(LogLevel::LOGERROR, "Failed to download file from Telegram.");
                res.status = 500;
                res.set_content("Failed to download file", "text/plain");
                return;
            }

            // 将文件数据缓存，并返回内容
            cacheManager.cacheImage(fileId, fileData, extension);
            res.set_content(fileData, mimeType);
            log(LogLevel::INFO, "Successfully served and cached file for file ID: " + fileId + " with MIME type: " + mimeType);
        } else {
            res.status = 404;
            res.set_content("File Not Found", "text/plain");
            log(LogLevel::LOGERROR, "File not found in Telegram for ID: " + fileId);
        }
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
        log(LogLevel::LOGERROR, "Error processing request for file ID: " + fileId + " - " + std::string(e.what()));
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