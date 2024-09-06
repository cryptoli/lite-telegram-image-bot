#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "server.h"
#include "request_handler.h"
#include "utils.h"
#include "httplib.h"
#include "bot.h"
#include <memory>
#include <fstream>
#include <vector>
#include <string>

// 手动实现 make_unique
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

std::string loadTemplate(const std::string& filepath) {
    std::ifstream file(filepath);
    if (file) {
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    } else {
        throw std::runtime_error("Unable to open template file: " + filepath);
    }
}

std::string generateImageGallery(const std::vector<std::string>& images) {
    std::string gallery;
    for (const auto& img : images) {
        gallery += "<div class=\"image-item\">";
        gallery += "<img src=\"/images/" + img + "\" alt=\"" + img + "\">";
        gallery += "</div>";
    }
    return gallery;
}

void startServer(const Config& config, ImageCacheManager& cacheManager, ThreadPool& pool, Bot& bot) {
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

    svr->Get(R"(/images/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl);
    });

    svr->Get(R"(/files/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl);
    });

    svr->Get(R"(/videos/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl);
    });

    svr->Get(R"(/audios/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl);
    });

    svr->Get(R"(/stickers/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl);
    });

    svr->Post("/webhook", [&bot, secretToken](const httplib::Request& req, httplib::Response& res) {
    log(LogLevel::INFO, "Headers:");
    for (const auto& header : req.headers) {
        log(LogLevel::INFO, header.first + ": " + header.second);
    }

    log(LogLevel::INFO, "Body: " + req.body);

    // 验证 X-Telegram-Bot-Api-Secret-Token
    if (req.has_header("X-Telegram-Bot-Api-Secret-Token")) {
        std::string receivedToken = req.get_header_value("X-Telegram-Bot-Api-Secret-Token");
        log(LogLevel::INFO, "Received Secret Token: " + receivedToken);

        if (receivedToken != secretToken) {
            res.set_content("Unauthorized", "text/plain");
            log(LogLevel::LOGERROR, "Unauthorized Webhook request due to invalid token");
            return;
        }
    } else {
        res.set_content("Unauthorized", "text/plain");
        log(LogLevel::LOGERROR, "Unauthorized Webhook request due to missing token");
        return;
    }

    try {
        nlohmann::json update = nlohmann::json::parse(req.body);

        // 打印 userID、username 和消息内容
        if (update.contains("message")) {
            const auto& message = update["message"];
            if (message.contains("from")) {
                std::string userId = std::to_string(message["from"]["id"].get<int64_t>());
                std::string username = message["from"].value("username", "unknown");
                std::string text = message.value("text", "No text provided");

                log(LogLevel::INFO, "User ID: " + userId + ", Username: " + username + ", Message: " + text);
            }
        }

        bot.handleWebhook(update);  // 处理请求体中的更新
        res.set_content("OK", "text/plain");
        log(LogLevel::INFO, "Processed Webhook update");
    } catch (const std::exception& e) {
        log(LogLevel::LOGERROR, "Error processing Webhook: " + std::string(e.what()));
        res.set_content("Bad Request", "text/plain");
    }
    });

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

    svr->Get("/", [&cacheManager](const httplib::Request& req, httplib::Response& res) {
        std::vector<std::string> images = {"image1.jpg", "image2.png", "image3.gif"};
        std::string galleryHtml = generateImageGallery(images);

        std::string html = loadTemplate("templates/index.html");
        size_t pos = html.find("{{gallery}}");
        if (pos != std::string::npos) {
            html.replace(pos, 11, galleryHtml);
        }
        res.set_content(html, "text/html");
    });

    std::future<void> serverFuture = pool.enqueue([&svr, hostname, port]() {
        log(LogLevel::INFO,"Server thread running on port: " + std::to_string(port));
        if (!svr->listen(hostname.c_str(), port)) {
            log(LogLevel::LOGERROR,"Error: Server failed to start on port: " + std::to_string(port));
        }
    });

    serverFuture.get();
}

