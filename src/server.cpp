#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "server.h"
#include "request_handler.h"
#include "utils.h"
#include "httplib.h"
#include "bot.h"
#include "db_manager.h"
#include "CacheManager.h"
#include <memory>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <chrono>

// 手动实现 make_unique
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

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

// 统一处理媒体请求
void handleMediaRequest(const httplib::Request& req, httplib::Response& res, const Config& config, CacheManager& cacheManager, const std::function<void(const httplib::Request&, httplib::Response&)>& handler) {
    std::string clientIp = getClientIp(req);
    
    // 使用 CacheManager 处理限流
    if (!cacheManager.checkRateLimit(clientIp, config.getRateLimitRequestsPerMinute())) {
        log(LogLevel::INFO,"IP:" + clientIp + " 已超过一分钟内最大请求次数，已限制请求");
        res.set_content("Too many requests", "text/plain");
        res.status = 429;
        return;
    }

    if (config.enableReferers() && req.has_header("Referer")) {
        std::string referer = req.get_header_value("Referer");
        if (!cacheManager.checkReferer(referer, config.getAllowedReferers())) {
            res.set_content("Forbidden", "text/plain");
            res.status = 403;
            return;
        }
    }

    handler(req, res);  // 调用实际处理函数
}

// 加载模板文件
std::string loadTemplate(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        throw std::runtime_error("Unable to open template file: " + filepath);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void startServer(const Config& config, ImageCacheManager& cacheManager, ThreadPool& pool, Bot& bot, CacheManager& rateLimiter) {
    std::string apiToken = config.getApiToken();
    std::string hostname = config.getHostname();
    std::string secretToken = config.getSecretToken();
    std::string telegramApiUrl = config.getTelegramApiUrl();
    int port = config.getPort();
    bool useHttps = config.getUseHttps();
    bool allowRegistration = config.getAllowRegistration();
    auto mimeTypes = config.getMimeTypes();

    std::unique_ptr<httplib::Server> svr;
    if (useHttps) {
        std::string certPath = config.getSslCertificate();
        std::string keyPath = config.getSslKey();
        svr = make_unique<httplib::SSLServer>(certPath.c_str(), keyPath.c_str());
    } else {
        svr = make_unique<httplib::Server>();
    }

    // 定义一个通用的媒体请求处理函数
    auto mediaRequestHandler = [&apiToken, &mimeTypes, &cacheManager, &rateLimiter, &telegramApiUrl, &config](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, rateLimiter, telegramApiUrl, config);
    };

    // 为五个路由设置通用的限流和 referer 验证
    svr->Get(R"(/images/([^\s/]+))", [&config, &rateLimiter, mediaRequestHandler](const httplib::Request& req, httplib::Response& res) {
        handleMediaRequest(req, res, config, rateLimiter, mediaRequestHandler);
    });
    svr->Get(R"(/files/([^\s/]+))", [&config, &rateLimiter, mediaRequestHandler](const httplib::Request& req, httplib::Response& res) {
        handleMediaRequest(req, res, config, rateLimiter, mediaRequestHandler);
    });
    svr->Get(R"(/videos/([^\s/]+))", [&config, &rateLimiter, mediaRequestHandler](const httplib::Request& req, httplib::Response& res) {
        handleMediaRequest(req, res, config, rateLimiter, mediaRequestHandler);
    });
    svr->Get(R"(/audios/([^\s/]+))", [&config, &rateLimiter, mediaRequestHandler](const httplib::Request& req, httplib::Response& res) {
        handleMediaRequest(req, res, config, rateLimiter, mediaRequestHandler);
    });
    svr->Get(R"(/stickers/([^\s/]+))", [&config, &rateLimiter, mediaRequestHandler](const httplib::Request& req, httplib::Response& res) {
        handleMediaRequest(req, res, config, rateLimiter, mediaRequestHandler);
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
        }
    });

    // 注册和登录页面路由
    svr->Get("/login", [](const httplib::Request& req, httplib::Response& res) {
        std::string html = loadTemplate("templates/login.html");
        res.set_content(html, "text/html");
    });

    svr->Get("/register", [allowRegistration](const httplib::Request& req, httplib::Response& res) {
        if (!allowRegistration) {
            res.set_content("<h1>Registration is not allowed.</h1>", "text/html");
            return;
        }
        std::string html = loadTemplate("templates/register.html");
        res.set_content(html, "text/html");
    });

    // 首页路由
    svr->Get("/", [](const httplib::Request& req, httplib::Response& res) {
        DBManager dbManager("bot_database.db");
        int page = req.has_param("page") ? std::stoi(req.get_param_value("page")) : 1;
        int pageSize = 10;

        std::vector<std::tuple<std::string, std::string, std::string, std::string>> mediaFiles = dbManager.getImagesAndVideos(page, pageSize);
        std::string galleryHtml;

        for (const auto& media : mediaFiles) {
            const std::string& fileName = std::get<1>(media);
            const std::string& fileLink = std::get<2>(media);
            const std::string& extension = std::get<3>(media);

            std::string mediaType = (extension == ".mp4" || extension == ".mkv" || extension == ".avi" || extension == ".mov") ? "video" : "image";
            galleryHtml += "<div class=\"media-item\">";
            if (mediaType == "image") {
                galleryHtml += "<img src=\"" + fileLink + "\" alt=\"" + fileName + "\" class=\"media-preview\">";
            } else {
                galleryHtml += "<video controls class=\"media-preview\"><source src=\"" + fileLink + "\" type=\"video/" + extension + "\"></video>";
            }
            galleryHtml += "<div class=\"media-name\">" + fileName + "</div></div>";
        }

        std::string html = loadTemplate("templates/index.html");
        html.replace(html.find("{{gallery}}"), 11, galleryHtml);
        res.set_content(html, "text/html");
    });

    // 启动服务器
    std::future<void> serverFuture = pool.enqueue([&svr, hostname, port]() {
        log(LogLevel::INFO,"Server running on port: " + std::to_string(port));
        if (!svr->listen(hostname.c_str(), port)) {
            log(LogLevel::LOGERROR,"Error: Server failed to start on port: " + std::to_string(port));
        }
    });

    serverFuture.get();
}
