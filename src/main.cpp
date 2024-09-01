#include "bot.h"
#include "http_client.h"
#include "thread_pool.h"
#include "config.h"
#include "utils.h"
#include <chrono>
#include <iostream>
#include <httplib.h>
#include <map>

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
    Config config("config.json");
    std::string apiToken = config.getApiToken();
    std::string hostname = config.getHostname();
    int port = config.getPort();
    auto mimeTypes = config.getMimeTypes();

    log("Starting server at " + hostname + ":" + std::to_string(port));

    ThreadPool pool(4); // 使用4个线程初始化线程池

    // 启动HTTP服务器
    httplib::Server svr;

    svr.Get(R"(/images/(\w+))", [&apiToken, &mimeTypes](const httplib::Request& req, httplib::Response& res) {
        if (req.matches.size() < 2) {
            res.status = 400;
            res.set_content("Bad Request", "text/plain");
            log("Bad request received.");
            return;
        }

        std::string fileId = req.matches[1];
        std::string telegramFileUrl = "https://api.telegram.org/bot" + apiToken + "/getFile?file_id=" + fileId;
        std::string fileResponse = sendHttpRequest(telegramFileUrl);

        try {
            nlohmann::json jsonResponse = nlohmann::json::parse(fileResponse);
            if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
                std::string filePath = jsonResponse["result"]["file_path"];
                std::string fileDownloadUrl = "https://api.telegram.org/file/bot" + apiToken + "/" + filePath;

                std::string imageData = sendHttpRequest(fileDownloadUrl);
                std::string mimeType = getMimeType(filePath, mimeTypes);

                // 返回图片数据
                res.set_content(imageData, mimeType);
                log("Successfully served image for file ID: " + fileId + " with MIME type: " + mimeType);
            } else {
                res.status = 404;
                res.set_content("File Not Found", "text/plain");
                log("File not found for ID: " + fileId);
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content("Internal Server Error", "text/plain");
            log("Error processing request for file ID: " + fileId + " - " + e.what());
        }
    });

    // 线程池来处理服务器的启动和监听
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
