#ifndef BOT_HANDLER_H
#define BOT_HANDLER_H

#include <string>
#include "bot.h"
#include "thread_pool.h"

void processBotUpdates(Bot& bot, ThreadPool& pool, int& lastOffset, const std::string& apiToken);

#endif
