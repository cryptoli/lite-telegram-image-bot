#include "server.h"
#include "request_handler.h"
#include "utils.h"
#include <httplib.h>

void startServer(const Config& config, ImageCacheManager& cacheManager, ThreadPool& pool) {
    std::string apiToken = config.getApiToken();
    std::string hostname = config.getHostname();
    int port = config.getPort();
    auto mimeTypes = config.getMimeTypes();

    // 使用 SSL 证书和密钥初始化 HTTPS 服务器
    httplib::SSLServer svr("server.crt", "server.key");

    svr.Get(R"(/images/([^\s/]+))", [&apiToken, &mimeTypes, &cacheManager](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager);
    });

    std::future<void> serverFuture = pool.enqueue([&svr, hostname, port]() {
        log(LogLevel::INFO,"Server thread running on port: " + std::to_string(port));
        if (!svr.listen(hostname.c_str(), port)) {
            log(LogLevel::ERROR,"Error: Server failed to start on port: " + std::to_string(port));
        }
    });

    serverFuture.get();
}
