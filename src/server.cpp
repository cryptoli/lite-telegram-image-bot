#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "server.h"
#include "request_handler.h"
#include "utils.h"
#include "httplib.h"
#include "bot.h"
#include "db_manager.h"
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

    svr->Get(R"(/images/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl, &config](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl, config);
    });

    svr->Get(R"(/files/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl, &config](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl, config);
    });

    svr->Get(R"(/videos/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl, &config](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl, config);
    });

    svr->Get(R"(/audios/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl, &config](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl, config);
    });

    svr->Get(R"(/stickers/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager, &telegramApiUrl, &config](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager, telegramApiUrl, config);
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

svr->Get("/", [](const httplib::Request& req, httplib::Response& res) {
    DBManager dbManager("bot_database.db");

    // 获取当前的分页数（默认为 1）
    int page = 1;
    if (req.has_param("page")) {
        page = std::stoi(req.get_param_value("page"));
    }
    int pageSize = 10; // 每页显示 10 个文件

    // 从数据库中获取图片和视频文件，并查询 extension 字段
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> mediaFiles = dbManager.getImagesAndVideos(page, pageSize);

    // 生成 HTML 代码
    std::string galleryHtml;
    for (const auto& media : mediaFiles) {
        // const std::string& fileId = std::get<0>(media);
        const std::string& fileName = std::get<1>(media);
        const std::string& fileLink = std::get<2>(media);
        const std::string& extension = std::get<3>(media);

        // 判断是图片还是视频
        std::string mediaType = (extension == ".mp4" || extension == ".mkv" || extension == ".avi" || extension == ".mov") ? "video" : "image";

        galleryHtml += "<div class=\"media-item\">";
        if (mediaType == "image") {
            galleryHtml += "<img src=\"" + fileLink + "\" alt=\"" + fileName + "\" class=\"media-preview\">";
        } else if (mediaType == "video") {
            galleryHtml += "<video controls class=\"media-preview\">";
            galleryHtml += "<source src=\"" + fileLink + "\" type=\"video/" + extension + "\">";
            galleryHtml += "Your browser does not support the video tag.";
            galleryHtml += "</video>";
        }
        galleryHtml += "<div class=\"media-name\">" + fileName + "</div>";
        galleryHtml += "</div>";
    }

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

