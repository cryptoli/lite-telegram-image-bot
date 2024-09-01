#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>

std::string sendHttpRequest(const std::string& url);
std::string buildTelegramUrl(const std::string& apiToken, const std::string& chatId, const std::string& text);

#endif
