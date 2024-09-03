#include "config.h"
#include "image_cache_manager.h"
#include "thread_pool.h"
#include "bot_handler.h"
#include "server.h"
#include "utils.h"
#include "http_client.h"
#include <thread>
#include <chrono>

void setWebhook(const std::string& apiToken, const std::string& webhookUrl, const std::string& secretToken) {
    while (true) {
        std::string setWebhookUrl = "https://api.telegram.org/bot" + apiToken + "/setWebhook?url=" + webhookUrl + "/webhook&secret_token=" +secretToken;
        log(LogLevel::INFO, "Trying to set Webhook with url: " + setWebhookUrl);
        std::string response = sendHttpRequest(setWebhookUrl);

        if (response.find("404") == std::string::npos) { 
            log(LogLevel::INFO,"Webhook set successfully. Response: " + response);
            break;
        } else {
            log(LogLevel::LOGERROR, "Failed to set Webhook. Response: " + response);
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }
}

int main(int argc, char* argv[]) {
    // 加载配置文件
    Config config("config.json");
    std::string apiToken = config.getApiToken();
    std::string secretToken = config.getSecretToken();

    log(LogLevel::INFO,"Starting application...");

    // 初始化线程池
    ThreadPool pool(4);

    // 创建 ImageCacheManager 实例，使用配置文件中的参数
    ImageCacheManager cacheManager("cache", config.getCacheMaxSizeMB(), config.getCacheMaxAgeSeconds());

    // 创建 Bot 实例
    Bot bot(apiToken);

    // 获取配置的 Webhook URL
    std::string webhookUrl = config.getWebhookUrl();

    // 设置 Webhook，处理404错误
    setWebhook(apiToken, webhookUrl, secretToken);

    // 启动服务器，在一个单独的线程中运行
    std::thread serverThread([&]() {
        startServer(config, cacheManager, pool, bot);
    });

    // 服务器线程在程序退出前正确关闭
    serverThread.join();

    return 0;
}
