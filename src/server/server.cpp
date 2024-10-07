#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "server/server.h"
#include "server/request_handler.h"
#include "utils.h"
#include "http/httplib.h"
#include "server/bot.h"
#include "db/db_manager.h"
#include "cache/CacheManager.h"
#include "http/http_client.h"
#include "server/PicGoHandler.h"
#include "Constant.h"
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
#include <unordered_map>

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

    auto registerMediaRoute = [&](const std::string& pattern) {
        svr->Get(pattern, [&config, &rateLimiter, mediaRequestHandler, &statisticsManager, &pool](const httplib::Request& req, httplib::Response& res) {
            unifiedInterceptor(req, res, config, rateLimiter, mediaRequestHandler, statisticsManager, pool); 
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
    svr->Post("/webhook", [&bot, &pool, secretToken](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Telegram-Bot-Api-Secret-Token") || req.get_header_value("X-Telegram-Bot-Api-Secret-Token") != secretToken) {
            res.set_content("Unauthorized", "text/plain");
            res.status = 401;
            return;
        }

        try {
            nlohmann::json update = nlohmann::json::parse(req.body);
            bot.handleWebhook(update, pool);
            res.set_content("OK", "text/plain");
        } catch (const std::exception& e) {
            log(LogLevel::LOGERROR, "Error processing Webhook: ", std::string(e.what()));
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

    std::future<void> serverFuture = pool.enqueue([&svr, hostname, port]() {
        log(LogLevel::INFO,"Server running on port: ", std::to_string(port));
        if (!svr->listen(hostname.c_str(), port)) {
            log(LogLevel::LOGERROR,"Error: Server failed to start on port: ", std::to_string(port));
        }
    });

    try {
        serverFuture.get();
    } catch (const std::system_error& e) {
        log(LogLevel::LOGERROR, "System error occurred: ", std::string(e.what()));
        throw;
    }
}
