#include "bot.h"
#include "http_client.h"
#include "thread_pool.h"
#include "config.h"
#include "utils.h"
#include "image_cache_manager.h"
#include <chrono>
#include <iostream>
#include <httplib.h>
#include <map>
#include <filesystem>
#include <regex>

// 获取文件的 MIME 类型
std::string getMimeType(const std::string& filePath, const std::map<std::string, std::string>& mimeTypes) {
    std::string extension = filePath.substr(filePath.find_last_of("."));
    auto it = mimeTypes.find(extension);
    if (it != mimeTypes.end()) {
        return it->second;
    } else {
        return "application/octet-stream";
    }
}

int main(int argc, char* argv[]) {
    // 加载配置文件
    Config config("config.json");
    std::string apiToken = config.getApiToken();
    std::string hostname = config.getHostname();
    int port = config.getPort();
    auto mimeTypes = config.getMimeTypes();
    int cacheMaxSizeMB = config.getCacheMaxSizeMB();
    int cacheMaxAgeSeconds = config.getCacheMaxAgeSeconds();

    log("Starting server at " + hostname + ":" + std::to_string(port));

    // 初始化线程池
    ThreadPool pool(4);

    // 创建 ImageCacheManager 实例，使用配置文件中的参数
    ImageCacheManager cacheManager("./cache", cacheMaxSizeMB, cacheMaxAgeSeconds);

    // 启动HTTP服务器
    httplib::Server svr;

    // 处理图片请求
    svr.Get(R"(/images/(\w+))", [&apiToken, &mimeTypes, &cacheManager](const httplib::Request& req, httplib::Response& res) {
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
    });

    // 线程池处理服务器的启动和监听
    std::future<void> serverFuture = pool.enqueue([&svr, hostname, port]() {
        log("Server thread running on port: " + std::to_string(port));
        if (!svr.listen(hostname.c_str(), port)) {
            log("Error: Server failed to start on port: " + std::to_string(port));
        }
    });

    // 处理来自 Telegram API 的更新
    Bot bot(apiToken);
    int lastOffset = bot.getSavedOffset();

    while (true) {
        std::string updatesUrl = "https://api.telegram.org/bot" + apiToken + "/getUpdates?offset=" + std::to_string(lastOffset + 1);
        std::string updatesResponse = sendHttpRequest(updatesUrl);

        if (updatesResponse.empty()) {
            log("Failed to get updates from Telegram API.");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        nlohmann::json updates;
        try {
            updates = nlohmann::json::parse(updatesResponse);
        } catch (nlohmann::json::parse_error& e) {
            log("JSON parse error: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        for (const auto& update : updates["result"]) {
            int updateId = update["update_id"];
            std::string userId = "Unknown";
            std::string userName = "Unknown";

            if (update.contains("message") && update["message"].contains("from")) {
                userId = update["message"]["from"]["id"].get<std::string>();
                userName = update["message"]["from"]["username"].get<std::string>();
            }

            log("Processing update from user ID: " + userId + ", Username: " + userName);

            pool.enqueue([&bot, update]() {
                bot.processUpdate(update);
            });

            lastOffset = updateId;
            bot.saveOffset(lastOffset);
            log("Processed update ID: " + std::to_string(updateId));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // 延时避免频繁请求
    }

    serverFuture.get();

    return 0;
}
