#include "config.h"
#include "image_cache_manager.h"
#include "thread_pool.h"
#include "bot_handler.h"
#include "server.h"
#include "utils.h"
#include "http_client.h"
#include <thread>

void setWebhook(const std::string& apiToken, const std::string& webhookUrl) {
    std::string setWebhookUrl = "https://api.telegram.org/bot" + apiToken + "/setWebhook?url=" + webhookUrl;
    log(LogLevel::INFO, "Webhook set url: " + setWebhookUrl);
    std::string response = sendHttpRequest(setWebhookUrl);
    log(LogLevel::INFO,"Webhook set response: " + response);
}

int main(int argc, char* argv[]) {
    // 加载配置文件
    Config config("config.json");
    std::string apiToken = config.getApiToken();

    log(LogLevel::INFO,"Starting application...");

    // 初始化线程池
    ThreadPool pool(4);

    // 创建 ImageCacheManager 实例，使用配置文件中的参数
    ImageCacheManager cacheManager("cache", config.getCacheMaxSizeMB(), config.getCacheMaxAgeSeconds());

    // 获取配置的 Webhook URL
    std::string webhookUrl = config.getWebhookUrl();

    // 设置 Webhook
    setWebhook(apiToken, webhookUrl);

    // 启动服务器，在一个单独的线程中运行
    std::thread serverThread([&]() {
        startServer(config, cacheManager, pool);
    });

    // 服务器线程在程序退出前正确关闭
    serverThread.join();

    return 0;
}
