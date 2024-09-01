#include "request_handler.h"
#include "http_client.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <regex>

std::string getMimeType(const std::string& filePath, const std::map<std::string, std::string>& mimeTypes) {
    std::string extension = filePath.substr(filePath.find_last_of("."));
    auto it = mimeTypes.find(extension);
    if (it != mimeTypes.end()) {
        return it->second;
    } else {
        return "application/octet-stream";
    }
}

void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken, const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager) {
    log("Received request for image.");

    if (req.matches.size() < 2) {
        res.status = 400;
        res.set_content("Bad Request", "text/plain");
        log("Bad request: URL does not match expected format.");
        return;
    }

    std::string fileId = req.matches[1];

    // 验证fileId的合法性
    std::regex fileIdRegex("^[A-Za-z0-9_-]+$");
    if (!std::regex_match(fileId, fileIdRegex)) {
        res.status = 400;
        res.set_content("Invalid File ID", "text/plain");
        log("Invalid file ID received: " + fileId);
        return;
    }

    log("Requesting file ID: " + fileId);

    // 尝试从缓存中获取图片数据
    std::string cachedImageData = cacheManager.getCachedImage(fileId);
    if (!cachedImageData.empty()) {
        // 如果缓存命中，返回缓存的数据
        std::string mimeType = getMimeType(fileId, mimeTypes);
        res.set_content(cachedImageData, mimeType);
        log("Cache hit: Served image for file ID: " + fileId + " from cache.");
        return;
    }

    // 如果缓存未命中，则从Telegram下载图片
    std::string telegramFileUrl = "https://api.telegram.org/bot" + apiToken + "/getFile?file_id=" + fileId;
    std::string fileResponse = sendHttpRequest(telegramFileUrl);
    log("Received response from Telegram for file ID: " + fileId);

    try {
        nlohmann::json jsonResponse = nlohmann::json::parse(fileResponse);
        if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
            std::string filePath = jsonResponse["result"]["file_path"];
            std::string fileDownloadUrl = "https://api.telegram.org/file/bot" + apiToken + "/" + filePath;
            log("File path obtained: " + filePath);

            std::string imageData = sendHttpRequest(fileDownloadUrl);
            if (imageData.empty()) {
                log("Failed to download image from Telegram.");
                res.status = 500;
                res.set_content("Failed to download image", "text/plain");
                return;
            }

            // 将图片数据缓存
            cacheManager.cacheImage(fileId, imageData);

            std::string mimeType = getMimeType(filePath, mimeTypes);
            log("MIME type determined: " + mimeType);

            // 返回图片数据
            res.set_content(imageData, mimeType);
            log("Successfully served image for file ID: " + fileId + " with MIME type: " + mimeType);
        } else {
            res.status = 404;
            res.set_content("File Not Found", "text/plain");
            log("File not found in Telegram for ID: " + fileId);
        }
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
        log("Error processing request for file ID: " + fileId + " - " + std::string(e.what()));
    }
}