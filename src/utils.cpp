#include "utils.h"
#include <regex>

bool isValidUrl(const std::string& url) {
    const std::regex pattern(R"(^https?:\/\/[^\s/$.?#].[^\s]*$)");
    return std::regex_match(url, pattern);
}