#include "dynamic_sqlite_thread_pool.h"
#include <iostream>

DynamicSQLiteThreadPool::DynamicSQLiteThreadPool(const std::string& dbFile, int minPoolSize, int maxPoolSize)
    : dbFile(dbFile), minPoolSize(minPoolSize), maxPoolSize(maxPoolSize), currentPoolSize(0) {
    initializePool(minPoolSize);

    // 启动一个线程定期监控连接池并动态调整大小
    std::thread(&DynamicSQLiteThreadPool::monitorPool, this).detach();
}

DynamicSQLiteThreadPool::~DynamicSQLiteThreadPool() {
    std::lock_guard<std::mutex> lock(poolMutex);
    while (!connectionPool.empty()) {
        sqlite3* conn = connectionPool.front();
        sqlite3_close(conn);
        connectionPool.pop();
    }
}

void DynamicSQLiteThreadPool::initializePool(int initialSize) {
    std::lock_guard<std::mutex> lock(poolMutex);
    for (int i = 0; i < initialSize; ++i) {
        sqlite3* conn = createConnection();
        if (conn) {
            connectionPool.push(conn);
            ++currentPoolSize;
        }
    }
}

sqlite3* DynamicSQLiteThreadPool::createConnection() {
    sqlite3* conn = nullptr;
    if (sqlite3_open(dbFile.c_str(), &conn) != SQLITE_OK) {
        std::cerr << "Failed to open SQLite database: " << dbFile << std::endl;
        return nullptr;
    }
    return conn;
}

void DynamicSQLiteThreadPool::closeConnection(sqlite3* conn) {
    if (conn) {
        sqlite3_close(conn);
    }
}

sqlite3* DynamicSQLiteThreadPool::acquireConnection() {
    std::unique_lock<std::mutex> lock(poolMutex);
    condition.wait(lock, [this]() { return !connectionPool.empty() || currentPoolSize < maxPoolSize; });

    if (!connectionPool.empty()) {
        sqlite3* conn = connectionPool.front();
        connectionPool.pop();
        return conn;
    } else if (currentPoolSize < maxPoolSize) {
        sqlite3* conn = createConnection();
        if (conn) {
            ++currentPoolSize;
        }
        return conn;
    }

    return nullptr; // 如果达到最大池大小，无法创建更多连接
}

void DynamicSQLiteThreadPool::releaseConnection(sqlite3* conn) {
    std::lock_guard<std::mutex> lock(poolMutex);
    connectionPool.push(conn);
    condition.notify_one();
}

void DynamicSQLiteThreadPool::adjustPoolSize() {
    std::lock_guard<std::mutex> lock(poolMutex);

    // 如果池中的连接数量超过最小池大小，释放多余的连接
    while (connectionPool.size() > minPoolSize) {
        sqlite3* conn = connectionPool.front();
        connectionPool.pop();
        closeConnection(conn);
        --currentPoolSize;
    }
}

void DynamicSQLiteThreadPool::monitorPool() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60)); // 每隔60秒检查一次
        adjustPoolSize();
    }
}
