#ifndef DYNAMIC_SQLITE_THREAD_POOL_H
#define DYNAMIC_SQLITE_THREAD_POOL_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <sqlite3.h>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>

class DynamicSQLiteThreadPool {
public:
    DynamicSQLiteThreadPool(const std::string& dbFile, int minPoolSize, int maxPoolSize);
    ~DynamicSQLiteThreadPool();

    // 获取一个可用的数据库连接
    sqlite3* acquireConnection();

    // 释放连接回线程池
    void releaseConnection(sqlite3* conn);

    // 动态调整连接池大小
    void adjustPoolSize();

private:
    std::queue<sqlite3*> connectionPool;
    std::mutex poolMutex;
    std::condition_variable condition;
    std::string dbFile;
    int minPoolSize;
    int maxPoolSize;
    std::atomic<int> currentPoolSize;

    // 初始化连接池
    void initializePool(int initialSize);

    // 创建一个新的数据库连接
    sqlite3* createConnection();

    // 释放数据库连接
    void closeConnection(sqlite3* conn);

    // 定期检查连接池并动态调整
    void monitorPool();
};

#endif