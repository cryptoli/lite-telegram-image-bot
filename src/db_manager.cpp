#include "db_manager.h"
#include "utils.h"
#include <iostream>

DBManager::DBManager(const std::string& dbFile) : db(nullptr), dbFile(dbFile) {}

DBManager::~DBManager() {
    if (db) {
        sqlite3_close(db);
        log(LogLevel::INFO, "Database connection closed.");
    }
}

void checkSQLiteMemoryUsage(sqlite3* db) {
    int memoryUsed, highWaterMark;
    sqlite3_status(SQLITE_STATUS_MEMORY_USED, &memoryUsed, &highWaterMark, 0);
    log(LogLevel::INFO, "SQLite Memory Used: " + std::to_string(memoryUsed) + " bytes");
    log(LogLevel::INFO, "SQLite Memory High Water Mark: " + std::to_string(highWaterMark) + " bytes");
}

bool DBManager::initialize() {
    log(LogLevel::INFO, "Using database file: " + dbFile);

    // 打开数据库
    int rc = sqlite3_open_v2(dbFile.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Can't open database: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        return false;
    }
    log(LogLevel::INFO, "Database opened successfully.");

    // 检查数据库文件完整性
    rc = sqlite3_exec(db, "PRAGMA integrity_check;", 0, 0, 0);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Database integrity check failed: " + std::string(sqlite3_errmsg(db)));
        // 如果完整性检查失败，尝试删除并重新创建数据库文件
        log(LogLevel::LOGERROR, "Database is corrupted, attempting to recreate.");
        std::remove(dbFile.c_str());
        rc = sqlite3_open_v2(dbFile.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK) {
            log(LogLevel::LOGERROR, "Failed to recreate database: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
            return false;
        }
        log(LogLevel::INFO, "Database recreated successfully.");
    } else {
        log(LogLevel::INFO, "Database integrity check passed.");
    }

    // 检查内存使用
    checkSQLiteMemoryUsage(db);

    // 设置SQLite的缓存大小和临时存储在内存中
    rc = sqlite3_exec(db, "PRAGMA cache_size = 10000;", 0, 0, 0);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to set cache size: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
    } else {
        log(LogLevel::INFO, "Cache size set to 10000 pages.");
    }

    rc = sqlite3_exec(db, "PRAGMA temp_store = MEMORY;", 0, 0, 0);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to set temp store to MEMORY: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
    } else {
        log(LogLevel::INFO, "Temp store set to MEMORY.");
    }

    // 启用或禁用WAL模式
    rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to set journal mode to WAL: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
    } else {
        log(LogLevel::INFO, "Journal mode set to WAL.");
    }

    // 创建数据库表
    return createTables();
}

bool DBManager::createTables() {
    const char* userTableSQL = "CREATE TABLE IF NOT EXISTS users ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "telegram_id INTEGER UNIQUE, "
                               "username TEXT, "
                               "is_banned BOOLEAN DEFAULT 0);";

    const char* fileTableSQL = "CREATE TABLE IF NOT EXISTS files ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "user_id INTEGER, "
                               "file_id TEXT, "
                               "file_link TEXT, "
                               "file_name TEXT, "
                               "is_valid BOOLEAN DEFAULT 1, "
                               "FOREIGN KEY (user_id) REFERENCES users(id));";

    const char* settingsTableSQL = "CREATE TABLE IF NOT EXISTS settings ("
                                   "key TEXT PRIMARY KEY, "
                                   "value TEXT);";

    char* errMsg = 0;

    // 创建用户表
    int rc = sqlite3_exec(db, userTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (User Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "User table created or exists already.");

    // 创建文件表
    rc = sqlite3_exec(db, fileTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "File table created or exists already.");

    // 创建设置表
    rc = sqlite3_exec(db, settingsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (Settings Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "Settings table created or exists already.");

    return true;
}

bool DBManager::addUserIfNotExists(const std::string& telegramId, const std::string& username) {
    std::string query = "SELECT id FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement (User): " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    bool userExists = (rc == SQLITE_ROW);
    sqlite3_finalize(stmt);

    log(LogLevel::INFO, std::string("User ") + (userExists ? "exists" : "does not exist") + " in the database.");

    if (!userExists) {
        sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);
        std::string insertUserSQL = "INSERT INTO users (telegram_id, username) VALUES (?, ?)";
        rc = sqlite3_prepare_v2(db, insertUserSQL.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            log(LogLevel::LOGERROR, "Failed to prepare INSERT statement (User): " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
            sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
            return false;
        }
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            log(LogLevel::LOGERROR, "Failed to insert new user: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
            sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
            return false;
        }

        sqlite3_exec(db, "COMMIT;", 0, 0, 0);
        log(LogLevel::INFO, "New user inserted successfully.");
    }

    return true;
}

bool DBManager::addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName) {
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);
    std::string insertFileSQL = "INSERT INTO files (user_id, file_id, file_link, file_name) VALUES ((SELECT id FROM users WHERE telegram_id = ?), ?, ?, ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, insertFileSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare INSERT statement (File): " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
        return false;
    }

    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fileId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, fileLink.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fileName.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to insert file record: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
        return false;
    }

    sqlite3_exec(db, "COMMIT;", 0, 0, 0);
    log(LogLevel::INFO, "File record inserted successfully.");
    return true;
}

bool DBManager::removeFile(const std::string& userId, const std::string& fileName) {
    std::string deleteSQL = "DELETE FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?) AND file_name = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare DELETE statement (File): " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        return false;
    }
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fileName.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to delete file record: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        return false;
    }
    log(LogLevel::INFO, "File record deleted successfully.");
    return true;
}

bool DBManager::banUser(const std::string& telegramId) {
    std::string updateSQL = "UPDATE users SET is_banned = 1 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, updateSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (User Ban): " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        return false;
    }
    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to ban user: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
        return false;
    }
    log(LogLevel::INFO, "User banned successfully.");
    return true;
}

std::vector<std::pair<std::string, std::string>> DBManager::getUserFiles(const std::string& userId) {
    std::vector<std::pair<std::string, std::string>> files;
    std::string selectSQL = "SELECT file_name, file_link FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?)";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, selectSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string fileName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string fileLink = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        files.emplace_back(fileName, fileLink);
    }
    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "Fetched " + std::to_string(files.size()) + " files for user ID: " + userId);
    return files;
}

void DBManager::setRegistrationOpen(bool isOpen) {
    std::string updateSQL = "INSERT OR REPLACE INTO settings (key, value) VALUES ('registration', ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, updateSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, isOpen ? "1" : "0", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to update registration setting: " + std::string(sqlite3_errmsg(db)) + " (Code: " + std::to_string(rc) + ")");
    } else {
        log(LogLevel::INFO, "Registration setting updated successfully.");
    }
}

bool DBManager::isRegistrationOpen() {
    std::string selectSQL = "SELECT value FROM settings WHERE key = 'registration'";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, selectSQL.c_str(), -1, &stmt, nullptr);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        bool isOpen = value == "1";
        log(LogLevel::INFO, "Registration is " + std::string(isOpen ? "open" : "closed") + ".");
        return isOpen;
    }
    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "Registration status not found, defaulting to closed.");
    return false;
}
