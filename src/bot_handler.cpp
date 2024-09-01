#include "bot_handler.h"
#include "http_client.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <iostream>

void processBotUpdates(Bot& bot, ThreadPool& pool, int& lastOffset, const std::string& apiToken) {
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
                userId = std::to_string(update["message"]["from"]["id"].get<int>());
                userName = update["message"]["from"].value("username", "Unknown");
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
}
