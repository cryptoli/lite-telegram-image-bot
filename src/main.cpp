#include "bot.h"
#include "http_client.h"
#include "thread_pool.h"
#include <chrono>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <Telegram API Token>" << std::endl;
        return 1;
    }

    std::string apiToken = argv[1];
    Bot bot(apiToken);
    ThreadPool pool(4);

    int lastOffset = bot.getSavedOffset();

    while (true) {
        std::string updatesUrl = "https://api.telegram.org/bot" + apiToken + "/getUpdates?offset=" + std::to_string(lastOffset + 1);
        std::string updatesResponse = sendHttpRequest(updatesUrl);

        if (updatesResponse.empty()) {
            std::cerr << "Failed to get updates from Telegram API." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        nlohmann::json updates;
        try {
            updates = nlohmann::json::parse(updatesResponse);
        } catch (nlohmann::json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        for (const auto& update : updates["result"]) {
            int updateId = update["update_id"];
            pool.enqueue(&Bot::processUpdate, &bot, update);
            lastOffset = updateId;
            bot.saveOffset(lastOffset);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // 延时避免频繁请求
    }

    return 0;
}
