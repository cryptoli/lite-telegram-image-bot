#ifndef STATISTICSMANAGER_H
#define STATISTICSMANAGER_H

#include <string>
#include <vector>
#include <tuple>
#include <chrono>
#include "db_manager.h"

// 定义 SQL 参数类型
class SQLParam {
public:
    enum class Type { Text, Int, Double };
    Type type;
    std::string textValue;
    int intValue;
    double doubleValue;

    // 构造函数重载
    SQLParam(const std::string& val) : type(Type::Text), textValue(val) {}
    SQLParam(int val) : type(Type::Int), intValue(val) {}
    SQLParam(double val) : type(Type::Double), doubleValue(val) {}
};

class StatisticsManager {
public:
    StatisticsManager(DBManager& dbManager);

    // 插入请求统计
    void insertRequestStatistics(const std::string& clientIp, const std::string& requestPath, const std::string& httpMethod,
                                 int responseTime, int statusCode, int responseSize, int requestSize, const std::string& fileType, int requestLatency);

    // 更新服务使用数据
    void updateServiceUsage(const std::chrono::time_point<std::chrono::system_clock>& periodStart, int totalRequests, int successfulRequests,
                            int failedRequests, int totalRequestSize, int totalResponseSize, int uniqueIps, int maxConcurrentRequests,
                            int maxResponseTime, int avgResponseTime);

    // 更新某个时间段内的 URL 访问次数
    void updateTopUrlsByPeriod(const std::chrono::time_point<std::chrono::system_clock>& periodStart, const std::string& url);

    // 更新历史 URL 访问次数
    void updateTopUrlsByHistory(const std::string& url);

    // 获取统计数据的各种函数
    int getTotalRequests();
    int getTotalTraffic();
    std::tuple<int, int> getAverageTraffic();
    std::tuple<int, int> getMaxSingleTraffic();
    int getUniqueIpCount();
    int getActiveIpCount(const std::chrono::time_point<std::chrono::system_clock>& periodStart);
    std::vector<std::tuple<std::string, int, int>> getIpRequestStatistics();
    std::vector<std::tuple<std::string, int>> getRequestMethodDistribution();
    std::vector<std::tuple<int, int>> getStatusCodeDistribution();
    std::vector<std::tuple<std::string, int>> getFileTypeDistribution();
    int getAverageResponseTime();
    int getMaxResponseTime();
    int get95thPercentileResponseTime();
    std::vector<std::tuple<std::string, int>> getResponseTimeDistribution();
    float getFailureRate();
    int getTimeoutRequestCount(int timeoutThreshold);
    std::tuple<int, int, int> getCurrentPeriodStatistics();
    std::tuple<int, int, int> getHistoricalStatistics();
    std::tuple<int, int> getDailyPeak();
    std::vector<std::tuple<std::string, int>> getTopUrlsByPeriod(const std::chrono::time_point<std::chrono::system_clock>& periodStart, int limit);
    std::vector<std::tuple<std::string, int>> getTopUrlsByHistory(int limit);

private:
    DBManager& dbManager;

    // 执行 SQL 插入或更新
    void executeSQL(const std::string& query, const std::vector<SQLParam>& params);

    // 执行统计查询，返回计数结果
    int executeCountQuery(const std::string& query, const std::vector<SQLParam>& params);

    // 执行分布查询
    std::vector<std::tuple<std::string, int>> executeDistributionQuery(const std::string& query);
};

#endif  
