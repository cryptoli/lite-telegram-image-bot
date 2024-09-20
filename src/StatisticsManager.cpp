// StatisticsManager.cpp

#include "StatisticsManager.h"
#include "db_manager.h"
#include "utils.h"
#include <sstream>
#include <iostream>

StatisticsManager::StatisticsManager(DBManager& dbManager) : dbManager(dbManager) {}

// 插入请求统计
void StatisticsManager::insertRequestStatistics(const std::string& clientIp, const std::string& requestPath, const std::string& httpMethod,
                                                int responseTime, int statusCode, int responseSize, int requestSize, const std::string& fileType, int requestLatency) {
    std::string query = "INSERT INTO request_statistics (client_ip, request_path, http_method, request_time, response_time, status_code, response_size, request_size, file_type, request_latency) "
                        "VALUES (?, ?, ?, datetime('now'), ?, ?, ?, ?, ?, ?)";
    std::vector<SQLParam> params = {
        clientIp,
        requestPath,
        httpMethod,
        responseTime,
        statusCode,
        responseSize,
        requestSize,
        fileType,
        requestLatency
    };
    executeSQL(query, params);

    // 更新URL统计信息
    updateTopUrlsByPeriod(std::chrono::system_clock::now(), requestPath);
    updateTopUrlsByHistory(requestPath);
}

// 更新服务使用数据
void StatisticsManager::updateServiceUsage(const std::chrono::time_point<std::chrono::system_clock>& periodStart, int totalRequests, int successfulRequests,
                                           int failedRequests, int totalRequestSize, int totalResponseSize, int uniqueIps, int maxConcurrentRequests,
                                           int maxResponseTime, int avgResponseTime) {
    std::string query = "INSERT OR REPLACE INTO service_usage (period_start, total_requests, successful_requests, failed_requests, "
                        "total_request_size, total_response_size, unique_ips, max_concurrent_requests, max_response_time, avg_response_time) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    std::vector<SQLParam> params = {
        static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(periodStart.time_since_epoch()).count()),
        totalRequests,
        successfulRequests,
        failedRequests,
        totalRequestSize,
        totalResponseSize,
        uniqueIps,
        maxConcurrentRequests,
        maxResponseTime,
        avgResponseTime
    };
    executeSQL(query, params);
}

// 更新某个时间段内的 URL 访问次数
void StatisticsManager::updateTopUrlsByPeriod(const std::chrono::time_point<std::chrono::system_clock>& periodStart, const std::string& url) {
    std::string query = "INSERT INTO top_urls_period (period_start, url, request_count) "
                        "VALUES (?, ?, 1) "
                        "ON CONFLICT(period_start, url) DO UPDATE SET request_count = request_count + 1";
    std::vector<SQLParam> params = {
        static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(periodStart.time_since_epoch()).count()),
        url
    };
    executeSQL(query, params);
}

// 更新历史 URL 访问次数
void StatisticsManager::updateTopUrlsByHistory(const std::string& url) {
    std::string query = "INSERT INTO top_urls_history (url, total_request_count) "
                        "VALUES (?, 1) "
                        "ON CONFLICT(url) DO UPDATE SET total_request_count = total_request_count + 1";
    std::vector<SQLParam> params = { url };
    executeSQL(query, params);
}

// 执行 SQL 插入或更新
void StatisticsManager::executeSQL(const std::string& query, const std::vector<SQLParam>& params) {
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;

    // 准备 SQL 语句
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        // 绑定参数
        for (size_t i = 0; i < params.size(); ++i) {
            int index = static_cast<int>(i + 1);
            const SQLParam& param = params[i];
            switch (param.type) {
                case SQLParam::Type::Text:
                    sqlite3_bind_text(stmt, index, std::move(param.textValue.c_str()), -1, SQLITE_TRANSIENT);
                    break;
                case SQLParam::Type::Int:
                    sqlite3_bind_int(stmt, index, param.intValue);
                    break;
                case SQLParam::Type::Double:
                    sqlite3_bind_double(stmt, index, param.doubleValue);
                    break;
            }
        }

        // 执行 SQL 语句
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            log(LogLevel::LOGERROR, query + "Failed to execute SQL statement: " + std::string(sqlite3_errmsg(db)));
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, query + "Failed to prepare SQL statement: " + std::string(sqlite3_errmsg(db)));
    }

    dbManager.releaseDbConnection(db);
}

// 获取总请求数
int StatisticsManager::getTotalRequests() {
    std::string query = "SELECT COUNT(*) FROM request_statistics";
    return executeCountQuery(query, {});
}

// 获取总流量消耗
int StatisticsManager::getTotalTraffic() {
    std::string query = "SELECT SUM(request_size + response_size) FROM request_statistics";
    return executeCountQuery(query, {});
}

// 获取平均流量
std::tuple<int, int> StatisticsManager::getAverageTraffic() {
    std::string query = "SELECT AVG(request_size), AVG(response_size) FROM request_statistics";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    std::tuple<int, int> result(0, 0);

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::get<0>(result) = sqlite3_column_int(stmt, 0);
            std::get<1>(result) = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return result;
}

// 获取最大单次流量
std::tuple<int, int> StatisticsManager::getMaxSingleTraffic() {
    std::string query = "SELECT MAX(request_size), MAX(response_size) FROM request_statistics";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    std::tuple<int, int> result(0, 0);

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::get<0>(result) = sqlite3_column_int(stmt, 0);
            std::get<1>(result) = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return result;
}

// 获取唯一 IP 数量
int StatisticsManager::getUniqueIpCount() {
    std::string query = "SELECT COUNT(DISTINCT client_ip) FROM request_statistics";
    return executeCountQuery(query, {});
}

// 获取活跃 IP 数量
int StatisticsManager::getActiveIpCount(const std::chrono::time_point<std::chrono::system_clock>& periodStart) {
    std::string query = "SELECT COUNT(DISTINCT client_ip) FROM request_statistics WHERE request_time >= datetime(?, 'unixepoch')";
    std::vector<SQLParam> params = {
        static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(periodStart.time_since_epoch()).count())
    };
    return executeCountQuery(query, params);
}

// 获取 IP 请求统计信息
std::vector<std::tuple<std::string, int, int>> StatisticsManager::getIpRequestStatistics() {
    std::vector<std::tuple<std::string, int, int>> stats;
    sqlite3* db = dbManager.getDbConnection();
    std::string query = "SELECT client_ip, COUNT(*), SUM(request_size + response_size) FROM request_statistics GROUP BY client_ip";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string ip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int requestCount = sqlite3_column_int(stmt, 1);
            int traffic = sqlite3_column_int(stmt, 2);
            stats.emplace_back(ip, requestCount, traffic);
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return stats;
}

// 请求方法分布
std::vector<std::tuple<std::string, int>> StatisticsManager::getRequestMethodDistribution() {
    return executeDistributionQuery("SELECT http_method, COUNT(*) FROM request_statistics GROUP BY http_method");
}

// 状态码分布
std::vector<std::tuple<int, int>> StatisticsManager::getStatusCodeDistribution() {
    std::vector<std::tuple<int, int>> stats;
    sqlite3* db = dbManager.getDbConnection();
    std::string query = "SELECT status_code, COUNT(*) FROM request_statistics GROUP BY status_code";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int statusCode = sqlite3_column_int(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            stats.emplace_back(statusCode, count);
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return stats;
}

// 文件类型分布
std::vector<std::tuple<std::string, int>> StatisticsManager::getFileTypeDistribution() {
    return executeDistributionQuery("SELECT file_type, COUNT(*) FROM request_statistics GROUP BY file_type");
}

// 获取平均响应时间
int StatisticsManager::getAverageResponseTime() {
    std::string query = "SELECT AVG(response_time) FROM request_statistics";
    return executeCountQuery(query, {});
}

// 获取最大响应时间
int StatisticsManager::getMaxResponseTime() {
    std::string query = "SELECT MAX(response_time) FROM request_statistics";
    return executeCountQuery(query, {});
}

// 获取 95% 响应时间
int StatisticsManager::get95thPercentileResponseTime() {
    std::string query = "SELECT response_time FROM request_statistics ORDER BY response_time LIMIT 1 OFFSET (SELECT COUNT(*) FROM request_statistics) * 95 / 100";
    return executeCountQuery(query, {});
}

// 获取响应时间分布
std::vector<std::tuple<std::string, int>> StatisticsManager::getResponseTimeDistribution() {
    return executeDistributionQuery("SELECT strftime('%H', request_time) AS hour, AVG(response_time) FROM request_statistics GROUP BY hour");
}

// 获取失败率
float StatisticsManager::getFailureRate() {
    std::string query = "SELECT (SELECT COUNT(*) FROM request_statistics WHERE status_code >= 400) * 1.0 / COUNT(*) FROM request_statistics";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    float failureRate = 0.0;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            failureRate = static_cast<float>(sqlite3_column_double(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return failureRate;
}

// 获取超时请求数
int StatisticsManager::getTimeoutRequestCount(int timeoutThreshold) {
    std::string query = "SELECT COUNT(*) FROM request_statistics WHERE response_time > ?";
    std::vector<SQLParam> params = { timeoutThreshold };
    return executeCountQuery(query, params);
}

// 获取当前时间段统计
std::tuple<int, int, int> StatisticsManager::getCurrentPeriodStatistics() {
    std::string query = "SELECT COUNT(*), SUM(request_size + response_size), COUNT(DISTINCT client_ip) FROM request_statistics WHERE request_time >= datetime('now', '-1 hour')";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    std::tuple<int, int, int> result(0, 0, 0);

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::get<0>(result) = sqlite3_column_int(stmt, 0);  // 请求数
            std::get<1>(result) = sqlite3_column_int(stmt, 1);  // 流量
            std::get<2>(result) = sqlite3_column_int(stmt, 2);  // IP 数量
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return result;
}

// 获取历史统计
std::tuple<int, int, int> StatisticsManager::getHistoricalStatistics() {
    std::string query = "SELECT COUNT(*), SUM(request_size + response_size), COUNT(DISTINCT client_ip) FROM request_statistics";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    std::tuple<int, int, int> result(0, 0, 0);

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::get<0>(result) = sqlite3_column_int(stmt, 0);  // 请求数
            std::get<1>(result) = sqlite3_column_int(stmt, 1);  // 流量
            std::get<2>(result) = sqlite3_column_int(stmt, 2);  // IP 数量
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return result;
}

// 获取每日峰值
std::tuple<int, int> StatisticsManager::getDailyPeak() {
    std::string query = "SELECT MAX(total_requests), MAX(total_request_size + total_response_size) FROM service_usage WHERE period_start >= datetime('now', '-1 day')";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    std::tuple<int, int> result(0, 0);

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::get<0>(result) = sqlite3_column_int(stmt, 0);  // 最大请求数
            std::get<1>(result) = sqlite3_column_int(stmt, 1);  // 最大流量
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return result;
}

// 获取某时间段请求次数最多的 URL
std::vector<std::tuple<std::string, int>> StatisticsManager::getTopUrlsByPeriod(const std::chrono::time_point<std::chrono::system_clock>& periodStart, int limit) {
    std::string query = "SELECT url, request_count FROM top_urls_period WHERE period_start >= ? ORDER BY request_count DESC LIMIT ?";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    std::vector<std::tuple<std::string, int>> result;
    std::vector<SQLParam> params = {
        static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(periodStart.time_since_epoch()).count()),
        limit
    };

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        // 绑定参数
        for (size_t i = 0; i < params.size(); ++i) {
            int index = static_cast<int>(i + 1);
            const SQLParam& param = params[i];
            if (param.type == SQLParam::Type::Int) {
                sqlite3_bind_int(stmt, index, param.intValue);
            } else if (param.type == SQLParam::Type::Text) {
                sqlite3_bind_text(stmt, index, std::move(param.textValue.c_str()), -1, SQLITE_TRANSIENT);
            }
        }

        // 执行查询并获取结果
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int requestCount = sqlite3_column_int(stmt, 1);
            result.emplace_back(url, requestCount);
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return result;
}

// 获取历史请求次数最多的 URL
std::vector<std::tuple<std::string, int>> StatisticsManager::getTopUrlsByHistory(int limit) {
    std::string query = "SELECT url, total_request_count FROM top_urls_history ORDER BY total_request_count DESC LIMIT ?";
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    std::vector<std::tuple<std::string, int>> result;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int requestCount = sqlite3_column_int(stmt, 1);
            result.emplace_back(url, requestCount);
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return result;
}

// 执行统计查询，返回计数结果
int StatisticsManager::executeCountQuery(const std::string& query, const std::vector<SQLParam>& params) {
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        // 绑定参数
        for (size_t i = 0; i < params.size(); ++i) {
            int index = static_cast<int>(i + 1);
            const SQLParam& param = params[i];
            if (param.type == SQLParam::Type::Int) {
                sqlite3_bind_int(stmt, index, param.intValue);
            } else if (param.type == SQLParam::Type::Text) {
                sqlite3_bind_text(stmt, index, std::move(param.textValue.c_str()), -1, SQLITE_TRANSIENT);
            }
        }

        // 执行查询并获取结果
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        // std::cerr << "Failed to prepare SQL statement: " << sqlite3_errmsg(db) << std::endl;
        log(LogLevel::LOGERROR, "executeCountQuery - Failed to prepare SQL statement: " + std::string(sqlite3_errmsg(db)));
    }

    dbManager.releaseDbConnection(db);
    return count;
}

// 执行分布查询
std::vector<std::tuple<std::string, int>> StatisticsManager::executeDistributionQuery(const std::string& query) {
    std::vector<std::tuple<std::string, int>> stats;
    sqlite3* db = dbManager.getDbConnection();
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        // 执行查询并获取结果
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int count = sqlite3_column_int(stmt, 1);
            stats.emplace_back(key, count);
        }
        sqlite3_finalize(stmt);
    }
    dbManager.releaseDbConnection(db);
    return stats;
}
