#include "config.h"
#include "image_cache_manager.h"
#include "thread_pool.h"
#include "bot_handler.h"
#include "server.h"
#include "logger.h"

int main(int argc, char* argv[]) {
    // 加载配置文件
    Config config("config.json");
    std::string apiToken = config.getApiToken();

    log("Starting application...");

    // 初始化线程池
    ThreadPool pool(4);

    // 创建 ImageCacheManager 实例，使用配置文件中的参数
    ImageCacheManager cacheManager("./cache", config.getCacheMaxSizeMB(), config.getCacheMaxAgeSeconds());

    // 启动服务器
    startServer(config, cacheManager, pool);

    // 处理来自 Telegram API 的更新
    Bot bot(apiToken);
    int lastOffset = bot.getSavedOffset();
    processBotUpdates(bot, pool, lastOffset);

    return 0;
}
