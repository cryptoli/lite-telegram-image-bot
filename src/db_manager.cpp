#include "db_manager.h"
#include <iostream>

DBManager::DBManager(const std::string& dbFile) : db(nullptr), dbFile(dbFile) {}

DBManager::~DBManager() {
    if (db) {
        sqlite3_close(db);
    }
}

bool DBManager::initialize() {
    int rc = sqlite3_open(dbFile.c_str(), &db);

    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

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
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    // 创建文件表
    rc = sqlite3_exec(db, fileTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    // 创建设置表
    rc = sqlite3_exec(db, settingsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

bool DBManager::addUserIfNotExists(const std::string& telegramId, const std::string& username) {
    // 检查用户是否已经存在
    std::string query = "SELECT id FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare SELECT statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    bool userExists = (rc == SQLITE_ROW);
    sqlite3_finalize(stmt);  // 确保释放资源

    // 如果用户不存在，插入新用户
    if (!userExists) {
        std::string insertUserSQL = "INSERT INTO users (telegram_id, username) VALUES (?, ?)";
        rc = sqlite3_prepare_v2(db, insertUserSQL.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare INSERT statement: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);  // 确保释放资源

        if (rc != SQLITE_DONE) {
            std::cerr << "Failed to insert new user: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
    }
    return true;
}

bool DBManager::addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName) {
    std::string insertFileSQL = "INSERT INTO files (user_id, file_id, file_link, file_name) VALUES ((SELECT id FROM users WHERE telegram_id = ?), ?, ?, ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, insertFileSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fileId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, fileLink.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fileName.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to insert file record: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DBManager::removeFile(const std::string& userId, const std::string& fileName) {
    std::string deleteSQL = "DELETE FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?) AND file_name = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, deleteSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fileName.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to delete file record: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

bool DBManager::banUser(const std::string& telegramId) {
    std::string updateSQL = "UPDATE users SET is_banned = 1 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, updateSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to ban user: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
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
        std::cerr << "Failed to update registration setting: " << sqlite3_errmsg(db) << std::endl;
    }
}

bool DBManager::isRegistrationOpen() {
    std::string selectSQL = "SELECT value FROM settings WHERE key = 'registration'";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, selectSQL.c_str(), -1, &stmt, nullptr);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        return value == "1";
    }
    sqlite3_finalize(stmt);
    return false;
}
