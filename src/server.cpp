#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "server.h"
#include "request_handler.h"
#include "utils.h"
#include "httplib.h"
#include "bot.h"
#include "db_manager.h"
#include "CacheManager.h"
#include "StatisticsManager.h"
#include "http_client.h"
#include "PicGoHandler.h"
#include <memory>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <algorithm>
#include <regex>
#include <future>
#include <nlohmann/json.hpp>

// 获取客户端真实 IP 地址
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

        // 以下统计数据需要根据实际情况计算，这里仅作为示例
        int totalRequests = 1;  // 当前请求计数为 1
        int successfulRequests = (statusCode >= 200 && statusCode < 300) ? 1 : 0;
        int failedRequests = (statusCode >= 400) ? 1 : 0;
        int totalRequestSize = requestSize;
        int totalResponseSize = responseSize;
        int uniqueIps = 1;  // 当前请求的 IP 数为 1
        int maxConcurrentRequests = 1;  // 简化处理，实际应用中应计算并发请求数
        int maxResponseTime = responseTime;
        int avgResponseTime = responseTime;

        statisticsManager.updateServiceUsage(periodStart, totalRequests, successfulRequests, failedRequests,
                                             totalRequestSize, totalResponseSize, uniqueIps,
                                             maxConcurrentRequests, maxResponseTime, avgResponseTime);
    });
}

// 确定文件类型
std::string determineFileType(const std::string& requestPath) {
    std::string fileType = "unknown";
    auto pos = requestPath.rfind('.');

    if (pos != std::string::npos) {
        std::string extension = requestPath.substr(pos + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower); // 转小写

        // 根据扩展名确定文件类型
        if (extension == "jpg" || extension == "jpeg" || extension == "png" || extension == "gif" || extension == "bmp" || extension == "svg" || extension == "webp") {
            fileType = "image";
        } else if (extension == "mp4" || extension == "mkv" || extension == "avi" || extension == "mov" || extension == "flv" || extension == "wmv") {
            fileType = "video";
        } else if (extension == "mp3" || extension == "wav" || extension == "aac" || extension == "flac" || extension == "ogg") {
            fileType = "audio";
        } else if (extension == "txt" || extension == "html" || extension == "css" || extension == "js" || extension == "json" || extension == "xml") {
            fileType = "text";
        } else if (extension == "pdf" || extension == "doc" || extension == "docx" || extension == "xls" || extension == "xlsx" || extension == "ppt" || extension == "pptx") {
            fileType = "document";
        } else if (extension == "zip" || extension == "rar" || extension == "7z" || extension == "tar" || extension == "gz") {
            fileType = "archive";
        } else {
            fileType = "other";
        }
    }

    return fileType;
}

// 加载模板文件
std::string loadTemplate(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        throw std::runtime_error("Unable to open template file: " + filepath);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void startServer(const Config& config, ImageCacheManager& cacheManager, ThreadPool& pool, Bot& bot, CacheManager& rateLimiter, DBManager& dbManager) {
    StatisticsManager statisticsManager(dbManager);

    std::string apiToken = config.getApiToken();
    std::string hostname = config.getHostname();
    std::string secretToken = config.getSecretToken();
    std::string telegramApiUrl = config.getTelegramApiUrl();
    int port = config.getPort();
    bool useHttps = config.getUseHttps();
    bool allowRegistration = config.getAllowRegistration();
    auto mimeTypes = config.getMimeTypes();

    PicGoHandler picGoHandler(config);

    std::unique_ptr<httplib::Server> svr;
    if (useHttps) {
        std::string certPath = config.getSslCertificate();
        std::string keyPath = config.getSslKey();
        svr = std::make_unique<httplib::SSLServer>(certPath.c_str(), keyPath.c_str());
    } else {
        svr = std::make_unique<httplib::Server>();
    }

    auto mediaRequestHandler = [&apiToken, &mimeTypes, &cacheManager, &rateLimiter, &telegramApiUrl, &config, &dbManager](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, rateLimiter, telegramApiUrl, config, dbManager);
    };

    // 为路由设置通用的限流、Referer 验证和统计处理
    auto registerMediaRoute = [&](const std::string& pattern) {
        svr->Get(pattern, [&config, &rateLimiter, mediaRequestHandler, &statisticsManager, &pool](const httplib::Request& req, httplib::Response& res) {
            // 记录请求到达时间
            auto requestArrivalTime = std::chrono::steady_clock::now();

            // 获取客户端 IP 地址
            // std::string clientIp = req.remote_addr;
            std::string clientIp;
            if (req.has_header("X-Forwarded-For")) {
                clientIp = req.get_header_value("X-Forwarded-For");
            } else if (req.has_header("X-Real-IP")) {
                clientIp = req.get_header_value("X-Real-IP");
            } else {
                clientIp = req.remote_addr;
            }
            std::string referer = req.get_header_value("Referer");
            log(LogLevel::INFO, "Request referer:  " + referer +", clientIP: " + clientIp);

            // 进行限流检查
            int maxRequestsPerMinute = config.getRateLimitRequestsPerMinute();
            if (!rateLimiter.checkRateLimit(clientIp, maxRequestsPerMinute)) {
                res.status = 429;
                res.set_content("Too Many Requests", "text/plain");
                return;
            }

            if (config.enableReferers()) {
                if (referer.empty()) {
                    res.status = 403;
                    res.set_content("Forbidden", "text/plain");
                    return;
                }

                // 获取允许的 Referer 列表
                std::vector<std::string> allowedReferers = config.getAllowedReferers();
                std::unordered_set<std::string> allowedReferersSet(allowedReferers.begin(), allowedReferers.end());

                // 检查 Referer 是否在允许的列表中
                if (!rateLimiter.checkReferer(referer, allowedReferersSet)) {
                    res.status = 403;
                    res.set_content("Forbidden", "text/plain");
                    return;
                }
            }
            auto startProcessingTime = std::chrono::steady_clock::now();

            // 计算请求延迟
            int requestLatency = std::chrono::duration_cast<std::chrono::milliseconds>(startProcessingTime - requestArrivalTime).count();
            handleMediaRequestWithTiming(req, res, config, rateLimiter, mediaRequestHandler, statisticsManager, pool, requestLatency);
        });
    };

    registerMediaRoute(R"(/images/(.*))");
    registerMediaRoute(R"(/files/(.*))");
    registerMediaRoute(R"(/videos/(.*))");
    registerMediaRoute(R"(/audios/(.*))");
    registerMediaRoute(R"(/stickers/(.*))");
    registerMediaRoute(R"(/d/(.*))");

    svr->Post("/upload", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Telegram-Bot-Api-Secret-Token") || req.get_header_value("X-Telegram-Bot-Api-Secret-Token") != secretToken) {
            res.set_content("Unauthorized", "text/plain");
            res.status = 401;
            return;
        }
        picGoHandler.handleUpload(req, res, config.getOwnerId(), "", dbManager);
    });

    // Webhook 路由
    svr->Post("/webhook", [&bot, secretToken](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Telegram-Bot-Api-Secret-Token") || req.get_header_value("X-Telegram-Bot-Api-Secret-Token") != secretToken) {
            res.set_content("Unauthorized", "text/plain");
            res.status = 401;
            return;
        }

        try {
            nlohmann::json update = nlohmann::json::parse(req.body);
            bot.handleWebhook(update);
            res.set_content("OK", "text/plain");
        } catch (const std::exception& e) {
            log(LogLevel::LOGERROR, "Error processing Webhook: " + std::string(e.what()));
            res.set_content("Bad Request", "text/plain");
            res.status = 400;
        }
    });

    // 注册和登录页面路由
    svr->Get("/login", [](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string html = loadTemplate("templates/login.html");
            res.set_content(html, "text/html");
        } catch (const std::exception& e) {
            res.set_content("Error loading page", "text/plain");
            res.status = 500;
        }
    });

    svr->Get("/register", [allowRegistration](const httplib::Request& req, httplib::Response& res) {
        if (!allowRegistration) {
            res.set_content("<h1>Registration is not allowed.</h1>", "text/html");
            return;
        }
        try {
            std::string html = loadTemplate("templates/register.html");
            res.set_content(html, "text/html");
        } catch (const std::exception& e) {
            res.set_content("Error loading page", "text/plain");
            res.status = 500;
        }
    });

    svr->Get("/pic", [&dbManager](const httplib::Request& req, httplib::Response& res) {
        int page = req.has_param("page") ? std::stoi(req.get_param_value("page")) : 1;
        int pageSize = 10;

        std::vector<std::tuple<std::string, std::string, std::string, std::string>> mediaFiles = dbManager.getImagesAndVideos(page, pageSize);
        std::string galleryHtml;

        for (const auto& media : mediaFiles) {
            const std::string& fileName = std::get<1>(media);
            const std::string& fileLink = std::get<2>(media);
            const std::string& extension = std::get<3>(media);

            std::string mediaType = (extension == ".mp4" || extension == ".mkv" || extension == ".avi" || extension == ".mov" || extension == ".flv" || extension == ".wmv") ? "video" : "image";
            galleryHtml += "<div class=\"media-item\">";
            if (mediaType == "image") {
                galleryHtml += "<img src=\"" + fileLink + "\" alt=\"" + fileName + "\" class=\"media-preview\">";
            } else {
                galleryHtml += "<video controls class=\"media-preview\"><source src=\"" + fileLink + "\" type=\"video/" + extension + "\"></video>";
            }
            galleryHtml += "<div class=\"media-name\">" + fileName + "</div></div>";
        }

        try {
            std::string html = loadTemplate("templates/index.html");
            size_t pos = html.find("{{gallery}}");
            if (pos != std::string::npos) {
                html.replace(pos, 11, galleryHtml);
            }
            res.set_content(html, "text/html");
        } catch (const std::exception& e) {
            res.set_content("Error loading page", "text/plain");
            res.status = 500;
        }
    });

    svr->Get("/", [allowRegistration](const httplib::Request& req, httplib::Response& res) {
        if (!allowRegistration) {
            res.set_content("<h1>Registration is not allowed.</h1>", "text/html");
            return;
        }
        try {
            std::string html = loadTemplate("templates/register.html");
            res.set_content(html, "text/html");
        } catch (const std::exception& e) {
            res.set_content("Error loading page", "text/plain");
            res.status = 500;
        }
    });

    // 启动服务器
    std::future<void> serverFuture = pool.enqueue([&svr, hostname, port]() {
        log(LogLevel::INFO,"Server running on port: " + std::to_string(port));
        if (!svr->listen(hostname.c_str(), port)) {
            log(LogLevel::LOGERROR,"Error: Server failed to start on port: " + std::to_string(port));
        }
    });

    try {
        serverFuture.get();
    } catch (const std::system_error& e) {
        log(LogLevel::LOGERROR, "System error occurred: " + std::string(e.what()));
        throw;
    }
}
