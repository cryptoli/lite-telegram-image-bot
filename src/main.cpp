#include "config.h"
#include "image_cache_manager.h"
#include "thread_pool.h"
#include "server.h"
#include "utils.h"
#include "http_client.h"
#include "db_manager.h"
#include "CacheManager.h"
#include <thread>
#include <chrono>
#include <exception>

void setWebhook(const std::string& apiToken, const std::string& webhookUrl, const std::string& secretToken, std::string& telegramApiUrl) {
    try {
        while (true) {
            std::string setWebhookUrl = telegramApiUrl + "/bot" + apiToken + "/setWebhook?url=" + webhookUrl + "/webhook&secret_token=" + secretToken;
            log(LogLevel::INFO, "Trying to set Webhook with url: " + setWebhookUrl);
            std::string response = sendHttpRequest(setWebhookUrl);

            if (response.find("404") == std::string::npos) { 
                log(LogLevel::INFO, "Webhook set successfully. Response: " + response);
                break;
            } else {
                log(LogLevel::LOGERROR, "Failed to set Webhook. Response: " + response);
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
        }
    } catch (const std::exception& e) {
        log(LogLevel::LOGERROR, "Exception in setWebhook: " + std::string(e.what()));
        throw;
    }
}

int main(int argc, char* argv[]) {
    try {
        // 设置SQLite为序列化模式，确保线程安全
        // sqlite3_config(SQLITE_CONFIG_SERIALIZED);

        // 初始化数据库
        DBManager dbManager("bot_database.db", 2, 20);
        if (!dbManager.initialize()) {
            std::cerr << "Init Database error...quit." << std::endl;
            log(LogLevel::LOGERROR, "Database initialization failed.");
            return 1;
        }

        // 加载配置文件
        Config config("config.json");
        std::string apiToken = config.getApiToken();
        std::string secretToken = config.getSecretToken();
        std::string telegramApiUrl = config.getTelegramApiUrl();

        log(LogLevel::INFO, "Starting application...");

        // 初始化线程池
        ThreadPool pool(4);

        // 创建 ImageCacheManager 实例，使用配置文件中的参数
        ImageCacheManager cacheManager("cache", config.getCacheMaxSizeMB(), config.getCacheMaxAgeSeconds());

        // 创建并启动缓存管理器（在单独的线程中运行）
        CacheManager cacheManagerSystem(100, 60);  // 最大缓存大小100，清理间隔60秒

        // 创建 Bot 实例
        Bot bot(apiToken, dbManager);

        // 获取配置的 Webhook URL
        std::string webhookUrl = config.getWebhookUrl();

        // 设置 Webhook，处理404错误
        setWebhook(apiToken, webhookUrl, secretToken, telegramApiUrl);

        // 启动服务器，在一个单独的线程中运行
        std::thread serverThread([&]() {
            try {
                startServer(config, cacheManager, pool, bot, cacheManagerSystem);
            } catch (const std::exception& e) {
                log(LogLevel::LOGERROR, "Exception in server thread: " + std::string(e.what()));
                throw; 
            }
        });

        // 等待服务器线程执行完成
        serverThread.join();

        // 停止缓存管理线程
        cacheManagerSystem.stopCleanupThread();
    } catch (const std::exception& e) {
        log(LogLevel::LOGERROR, "Exception caught in main: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        log(LogLevel::LOGERROR, "Unknown fatal error occurred.");
        std::cerr << "Unknown fatal error occurred." << std::endl;
        return 1;
    }

    return 0;
}