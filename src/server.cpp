#include "server.h"
#include "request_handler.h"
#include "utils.h"

void startServer(const Config& config, ImageCacheManager& cacheManager, ThreadPool& pool) {
    std::string apiToken = config.getApiToken();
    std::string hostname = config.getHostname();
    int port = config.getPort();
    auto mimeTypes = config.getMimeTypes();

    httplib::Server svr;

    svr.Get(R"(/images/(\w+))", [&apiToken, &mimeTypes, &cacheManager](const httplib::Request& req, httplib::Response& res) {
        handleImageRequest(req, res, apiToken, mimeTypes, cacheManager);
    });

    std::future<void> serverFuture = pool.enqueue([&svr, hostname, port]() {
        log(LogLevel::INFO,"Server thread running on port: " + std::to_string(port));
        if (!svr.listen(hostname.c_str(), port)) {
            log(LogLevel::INFO,"Error: Server failed to start on port: " + std::to_string(port));
        }
    });

    serverFuture.get();
}
