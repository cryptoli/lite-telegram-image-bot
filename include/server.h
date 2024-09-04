#ifndef SERVER_H
#define SERVER_H

#include "httplib.h"
#include "config.h"
#include "image_cache_manager.h"
#include "thread_pool.h"
#include "bot.h"

void startServer(const Config& config, ImageCacheManager& cacheManager, ThreadPool& pool, Bot& bot);

#endif
