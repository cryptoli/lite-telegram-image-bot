#include "server/request_handler.h"
#include "http/http_client.h"
#include "utils.h"
#include "config.h"
#include "db/db_manager.h"
#include "Constant.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <curl/curl.h>
#include <algorithm>
#include <future>
#include <sstream>

std::string getMimeType(const std::string& filePath, const std::map<std::string, std::string>& mimeTypes, const std::string& defaultMimeType = "application/octet-stream") {
    try {
        // 查找文件扩展名
        size_t pos = filePath.find_last_of(".");
        std::string extension;
        
        if (pos != std::string::npos && pos != filePath.length() - 1) {
            extension = filePath.substr(pos);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        }
        if (extension.empty() || extension == "bin") {
            if (filePath.find("photo") != std::string::npos) {
                return "image/jpeg";
            }
            if (filePath.find("video") != std::string::npos) {
                return "video/mp4";
            }
        }
        auto it = mimeTypes.find(extension);
        if (it != mimeTypes.end()) {
            return it->second;
        } else {
            return defaultMimeType;
        }
    } catch (const std::exception& e) {
        return defaultMimeType;
    }
}

std::string getFileExtension(const std::string& filePath) {
    std::size_t pos = filePath.find_last_of(".");
    if (pos != std::string::npos) {
        return filePath.substr(pos);
    }
    return "";
}

// 处理流式传输的回调函数，支持分段传输
size_t streamWriteCallback(void* ptr, size_t size, size_t nmemb, httplib::Response* res) {
    size_t totalSize = size * nmemb;
    res->body.append(static_cast<char*>(ptr), totalSize);
    return totalSize;
}

void handleStreamRequest(const httplib::Request& req, httplib::Response& res, const std::string& fileDownloadUrl, const std::string& mimeType) {
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, fileDownloadUrl.c_str());

        // 启用 HTTP Keep-Alive
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);

        // 增加缓冲区大小
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);

        // 设置请求超时
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        // 设置分块传输
        // res.set_header("Transfer-Encoding", "chunked");

        // 设置回调函数，流式传输数据
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);

        // 执行请求
        curl_easy_perform(curl);

        // 清理 curl 资源
        curl_easy_cleanup(curl);
    }
    
    res.set_header("Content-Type", mimeType);
    res.set_header("Accept-Ranges", "bytes");  // 支持分段下载
}

std::string getClientIp(const httplib::Request& req) {
    if (req.has_header("X-Forwarded-For")) {
        std::string forwardedFor = req.get_header_value("X-Forwarded-For");
        size_t commaPos = forwardedFor.find(',');
        if (commaPos != std::string::npos) {
            return forwardedFor.substr(0, commaPos);
        }
        return forwardedFor;
    }
    if (req.has_header("X-Real-IP")) {
        return req.get_header_value("X-Real-IP");
    }
    return req.remote_addr;
}

std::string determineFileType(const std::string& requestPath) {

    auto pos = requestPath.rfind('.');
    if (pos != std::string::npos) {
        std::string extension = requestPath.substr(pos + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        auto it = extensionMap.find(extension);
        if (it != extensionMap.end()) {
            return it->second;
        }
    }
    
    return "unknown";
}

void handleMediaRequestWithTiming(const httplib::Request& req, httplib::Response& res, const Config& config, CacheManager& cacheManager,
                                  const std::function<void(const httplib::Request&, httplib::Response&)>& handler,
                                  StatisticsManager& statisticsManager, ThreadPool& pool, int requestLatency) {
    // 记录开始处理请求的时间
    auto startProcessingTime = std::chrono::steady_clock::now();

    // 调用实际处理函数
    handler(req, res);

    // 记录完成处理请求的时间
    auto endProcessingTime = std::chrono::steady_clock::now();

    // 计算响应时间
    int responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(endProcessingTime - startProcessingTime).count();

    // 调用统计函数
    handleRequestStatistics(req, res, req.path, statisticsManager, pool, responseTime, requestLatency);
}

// 处理请求统计信息
void handleRequestStatistics(const httplib::Request& req, httplib::Response& res, const std::string& requestPath,
                             StatisticsManager& statisticsManager, ThreadPool& pool, int responseTime, int requestLatency) {
    // 获取客户端 IP 地址
    std::string clientIp = getClientIp(req);

    // 计算响应大小和请求大小
    int responseSize = static_cast<int>(res.body.size());  // 响应的字节大小
    int requestSize = static_cast<int>(req.body.size());   // 请求的字节大小

    // 获取状态码
    int statusCode = res.status;  // HTTP 响应状态码

    // 获取请求方法
    std::string httpMethod = req.method;

    // 获取文件类型（根据请求路径确定）
    std::string fileType = determineFileType(requestPath);

    // 异步统计处理
    pool.enqueue([=, &statisticsManager]() {
        // 插入请求统计数据
        statisticsManager.insertRequestStatistics(clientIp, requestPath, httpMethod, responseTime, statusCode,
                                                  responseSize, requestSize, fileType, requestLatency);

        // 更新服务使用统计数据
        auto periodStart = std::chrono::system_clock::now();
        int totalRequests = 1;  // 当前请求计数为 1
        int successfulRequests = (statusCode >= 200 && statusCode < 300) ? 1 : 0;
        int failedRequests = (statusCode >= 400) ? 1 : 0;
        int totalRequestSize = requestSize;
        int totalResponseSize = responseSize;
        int uniqueIps = 1;  // 当前请求的 IP 数为 1
        int maxConcurrentRequests = 1;
        int maxResponseTime = responseTime;
        int avgResponseTime = responseTime;

        statisticsManager.updateServiceUsage(periodStart, totalRequests, successfulRequests, failedRequests,
                                             totalRequestSize, totalResponseSize, uniqueIps,
                                             maxConcurrentRequests, maxResponseTime, avgResponseTime);
    });
}

// 统一拦截
void unifiedInterceptor(const httplib::Request& req, httplib::Response& res, const Config& config, CacheManager& rateLimiter,
                       std::function<void(const httplib::Request&, httplib::Response&)> handler,
                       StatisticsManager& statisticsManager, ThreadPool& pool) {
    // 1. 记录请求到达时间
    auto requestArrivalTime = std::chrono::steady_clock::now();

    // 2. 获取客户端 IP 地址和 Referer
    std::string clientIp = getClientIp(req);
    std::string referer = req.get_header_value("Referer");
    log(LogLevel::INFO, "Request referer:  ", referer,", clientIP: ", clientIp);

    // 3. 限流检查
    int maxRequestsPerMinute = config.getRateLimitRequestsPerMinute();
    if (!rateLimiter.checkRateLimit(clientIp, maxRequestsPerMinute)) {
        res.status = 429;
        res.set_content("Too Many Requests", "text/plain");
        return; 
    }

    // 4. Referer 验证
    if (config.enableReferers()) {
        if (referer.empty()) {
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return; 
        }

        std::vector<std::string> allowedReferers = config.getAllowedReferers();
        std::unordered_set<std::string> allowedReferersSet(allowedReferers.begin(), allowedReferers.end());

        if (!rateLimiter.checkReferer(referer, allowedReferersSet)) {
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return; 
        }
    }

    // 5. 登录认证 TODO

    // 6. 调用实际处理函数
    auto startProcessingTime = std::chrono::steady_clock::now();
    int requestLatency = std::chrono::duration_cast<std::chrono::milliseconds>(startProcessingTime - requestArrivalTime).count();
    handleMediaRequestWithTiming(req, res, config, rateLimiter, handler, statisticsManager, pool, requestLatency); 
}

void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken, const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager, CacheManager& memoryCache, const std::string& telegramApiUrl, const Config& config, DBManager& dbManager) {
    if (req.matches.size() < 2) {
        res.status = 400;
        res.set_content("Bad Request", "text/plain");
        log(LogLevel::LOGERROR, "Bad request: URL does not match expected format.");
        return;
    }

    std::string shortId = req.matches[1];
    std::string fileId = (shortId.length() > urlLength) ? shortId : dbManager.getFileIdByShortId(shortId);

    // 验证 fileId 的合法性
    std::regex fileIdRegex(FILE_ID_REGEX);
    if (!std::regex_match(fileId, fileIdRegex)) {
        res.status = 400;
        res.set_content("Invalid File ID", "text/plain");
        log(LogLevel::LOGERROR, "Invalid file ID received: ", fileId);
        return;
    }

    log(LogLevel::INFO, "Checking file path from memory cache for file ID: ", fileId);

    // Step 1: 从 memoryCache 中获取 filePath 是否存在
    std::string cachedFilePath;
    bool isMemoryCacheHit = memoryCache.getFilePathCache(fileId, cachedFilePath);

    // 获取文件的扩展名，默认为空字符串
    std::string preferredExtension = (req.has_header("Accept") && req.get_header_value("Accept").find("image/webp") != std::string::npos) ? "webp" : getFileExtension(cachedFilePath);

    // 如果 memory 缓存命中，检查 image 缓存（磁盘）是否命中
    if (isMemoryCacheHit) {
        log(LogLevel::INFO, "Memory cache hit for file ID: ", fileId, ". Checking image cache.");
        std::string cachedImageData = cacheManager.getCachedImage(fileId, preferredExtension);

        if (!cachedImageData.empty()) {
            log(LogLevel::INFO, "Image cache hit for file ID: ", fileId);
            // 获取文件的 MIME 类型
            std::string mimeType = getMimeType(cachedFilePath, mimeTypes);
            // 返回缓存的文件数据
            setHttpResponse(res, cachedImageData, mimeType, req);
            return;
        } else {
            log(LogLevel::INFO, "Image cache miss for file ID: ", fileId, ". Downloading from Telegram.");
        }
    } else {
        log(LogLevel::INFO, "Memory cache miss. Requesting file information from Telegram for file ID: ", fileId);

        // 如果 memoryCache 中没有 filePath，调用 getFile 接口获取文件路径
        std::string telegramFileUrl = telegramApiUrl + "/bot" + apiToken + "/getFile?file_id=" + fileId;
        std::string fileResponse = sendHttpRequest(telegramFileUrl);

        if (fileResponse.empty()) {
            res.status = 500;
            res.set_content("Failed to get file information from Telegram", "text/plain");
            log(LogLevel::LOGERROR, "Failed to retrieve file information from Telegram.");
            return;
        }

        nlohmann::json jsonResponse = nlohmann::json::parse(fileResponse);
        if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
            cachedFilePath = jsonResponse["result"]["file_path"];
            log(LogLevel::INFO, "Retrieved file path: ", cachedFilePath);

            memoryCache.addFilePathCache(fileId, cachedFilePath, 3600);
        } else {
            res.status = 404;
            res.set_content("File Not Found", "text/plain");
            log(LogLevel::LOGERROR, "File not found in Telegram for ID: ", fileId);
            return;
        }
    }

    // 获取文件 MIME 类型
    std::string mimeType = getMimeType(cachedFilePath, mimeTypes);

    // 如果文件是视频或文档，直接流式传输而不缓存
    if (mimeType.find("video") != std::string::npos || mimeType.find("application") != std::string::npos) {
        log(LogLevel::INFO, "Streaming file directly from Telegram (no caching) for MIME type: ", mimeType);
        std::string telegramFileDownloadUrl = telegramApiUrl + "/file/bot" + apiToken + "/" + cachedFilePath;
        handleStreamRequest(req, res, telegramFileDownloadUrl, mimeType);
        return;
    }

    // 从 Telegram 下载文件
    std::string telegramFileDownloadUrl = telegramApiUrl + "/file/bot" + apiToken + "/" + cachedFilePath;
    std::string fileData = sendHttpRequest(telegramFileDownloadUrl);

    if (fileData.empty()) {
        res.status = 500;
        res.set_content("Failed to download file from Telegram", "text/plain");
        log(LogLevel::LOGERROR, "Failed to download file from Telegram for file path: ", cachedFilePath);
        return;
    }

    std::future<void> cacheFuture = std::async(std::launch::async, [&cacheManager, fileId, fileData, preferredExtension]() {
        cacheManager.cacheImage(fileId, fileData, preferredExtension);
    });

    // 返回文件
    setHttpResponse(res, fileData, mimeType, req);
    log(LogLevel::INFO, "Successfully served and cached file for file ID: ", fileId);
}

void setHttpResponse(httplib::Response& res, const std::string& fileData, const std::string& mimeType, const httplib::Request& req) {
    res.set_header("Cache-Control", "max-age=3600");

    // 对小文件启用压缩以节省内存占用
    if (fileData.size() < 1048576 && req.has_header("Accept-Encoding") && req.get_header_value("Accept-Encoding").find("gzip") != std::string::npos) {
        res.set_content(gzipCompress(fileData), mimeType);
        res.set_header("Content-Encoding", "gzip");
    } else {
        res.set_content(fileData, mimeType);
    }
}

std::string getBaseUrl(const std::string& url) {
    std::regex urlRegex(R"((https?:\/\/[^\/:]+(:\d+)?))");
    std::smatch match;
    if (std::regex_search(url, match, urlRegex)) {
        return match.str(0);
    }
    return "";
}