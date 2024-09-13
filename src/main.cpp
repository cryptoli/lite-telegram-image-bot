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
#include <iostream>
#include <stdexcept>

void setWebhook(const std::string& apiToken, const std::string& webhookUrl, const std::string& secretToken, std::string& telegramApiUrl) {
    try {
        while (true) {
            std::string setWebhookUrl = telegramApiUrl + "/bot" + apiToken + "/setWebhook?url=" + webhookUrl + "/webhook&secret_token=" + secretToken;
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
    } catch (const std::system_error& e) {
        log(LogLevel::LOGERROR, "System error occurred in setWebhook: " + std::string(e.what()));
    } catch (const std::exception& e) {
        log(LogLevel::LOGERROR, "An error occurred in setWebhook: " + std::string(e.what()));
    }
}

int main(int argc, char* argv[]) {
    try {
        // 设置SQLite为序列化模式，确保线程安全
        // if (sqlite3_config(SQLITE_CONFIG_SERIALIZED) != SQLITE_OK) {
        //     throw std::runtime_error("Failed to configure SQLite for serialization");
        // }

        // 初始化数据库
        DBManager& dbManager = DBManager::getInstance();
        if (!dbManager.createTables()) {
            std::cerr << "Init Database error...quit." << std::endl;
            return 1;
        }

        // 加载配置文件
        Config config("config.json");
        std::string apiToken = config.getApiToken();
        std::string secretToken = config.getSecretToken();
        std::string telegramApiUrl = config.getTelegramApiUrl();

        log(LogLevel::INFO,"Starting application...");

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
                startServer(config, cacheManager, pool, bot, cacheManagerSystem, dbManager);
            } catch (const std::exception& e) {
                log(LogLevel::LOGERROR, "An error occurred in the server thread: " + std::string(e.what()));
            }
        });

        // 服务器线程在程序退出前正确关闭
        serverThread.join();

        // 停止缓存管理线程
        cacheManagerSystem.stopCleanupThread();

    } catch (const std::system_error& e) {
        log(LogLevel::LOGERROR, "System error occurred in main: " + std::string(e.what()));
        return 1;
    } catch (const std::exception& e) {
        log(LogLevel::LOGERROR, "An error occurred in main: " + std::string(e.what()));
        return 1;
    }

    return 0;
}
