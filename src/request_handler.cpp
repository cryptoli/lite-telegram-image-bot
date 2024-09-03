#include "request_handler.h"
#include "http_client.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <regex>

std::string getMimeType(const std::string& filePath, const std::map<std::string, std::string>& mimeTypes, const std::string& defaultMimeType = "application/octet-stream") {
    try {
        size_t pos = filePath.find_last_of(".");
        if (pos == std::string::npos || pos == filePath.length() - 1) {
            return defaultMimeType;
        }

        std::string extension = filePath.substr(pos);
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

void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken, const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager) {
    log(LogLevel::INFO,"Received request for image.");

    if (req.matches.size() < 2) {
        res.status = 400;
        res.set_content("Bad Request", "text/plain");
        log(LogLevel::ERROR,"Bad request: URL does not match expected format.");
        return;
    }

    std::string fileId = req.matches[1];

    // 验证fileId的合法性
    std::regex fileIdRegex("^[A-Za-z0-9_-]+$");
    if (!std::regex_match(fileId, fileIdRegex)) {
        res.status = 400;
        res.set_content("Invalid File ID", "text/plain");
        log(LogLevel::ERROR,"Invalid file ID received: " + fileId);
        return;
    }

    log(LogLevel::INFO,"Requesting file ID: " + fileId);

    // 获取 Telegram 文件信息的 URL
    std::string telegramFileUrl = "https://api.telegram.org/bot" + apiToken + "/getFile?file_id=" + fileId;
    log(LogLevel::INFO,"Request url: " + telegramFileUrl);
    std::string fileResponse = sendHttpRequest(telegramFileUrl);
    log(LogLevel::INFO,"Received response from Telegram for file ID: " + fileId);

    try {
        nlohmann::json jsonResponse = nlohmann::json::parse(fileResponse);
        if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
            std::string filePath = jsonResponse["result"]["file_path"];
            std::string extension = getFileExtension(filePath);
            std::string fileDownloadUrl = "https://api.telegram.org/file/bot" + apiToken + "/" + filePath;
            log(LogLevel::INFO,"File path obtained: " + filePath);

            // 尝试从缓存中获取图片数据
            std::string cachedImageData = cacheManager.getCachedImage(fileId, extension);
            if (!cachedImageData.empty()) {
                log(LogLevel::INFO,"缓存命中");
                std::string mimeType = getMimeType(fileId + extension, mimeTypes);
                res.set_content(cachedImageData, mimeType);
                log(LogLevel::WARNING,"Cache hit: Served image for file ID: " + fileId + " from cache.");
                return;
            }

            std::string imageData = sendHttpRequest(fileDownloadUrl);
            if (imageData.empty()) {
                log(LogLevel::ERROR,"Failed to download image from Telegram.");
                res.status = 500;
                res.set_content("Failed to download image", "text/plain");
                return;
            }

            // 将图片数据缓存，并指定扩展名
            cacheManager.cacheImage(fileId, imageData, extension);

            std::string mimeType = getMimeType(filePath, mimeTypes);
            log(LogLevel::INFO,"MIME type determined: " + mimeType);

            // 返回图片数据
            res.set_content(imageData, mimeType);
            log(LogLevel::INFO,"Successfully served image for file ID: " + fileId + " with MIME type: " + mimeType);
        } else {
            res.status = 404;
            res.set_content("File Not Found", "text/plain");
            log(LogLevel::ERROR,"File not found in Telegram for ID: " + fileId);
        }
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
        log(LogLevel::ERROR,"Error processing request for file ID: " + fileId + " - " + std::string(e.what()));
    }
}
