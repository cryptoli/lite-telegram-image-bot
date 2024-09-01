#include "bot.h"
#include "http_client.h"
#include "thread_pool.h"
#include "config.h"
#include "utils.h"
#include <chrono>
#include <iostream>
#include <httplib.h>
#include <map>

// 定义 getMimeType 函数
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
    // 从配置文件中读取 API Token
    Config config("config.json");
    std::string apiToken = config.getApiToken();
    
    Bot bot(apiToken);
    ThreadPool pool(4);

    try {
        std::string hostname = config.getHostname();
        int port = config.getPort();
        auto mimeTypes = config.getMimeTypes();

        log("Starting server at " + hostname + ":" + std::to_string(port));

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

        std::thread server_thread([&svr, hostname, port]() {
            log("Server thread running...");
            svr.listen(hostname.c_str(), port);
        });

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

        server_thread.join();

    } catch (const std::exception& e) {
        log("Error initializing server: " + std::string(e.what()));
        return 1;
    }

    return 0;
}
