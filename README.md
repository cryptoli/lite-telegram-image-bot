# Telegram Image Bot

Lite Telegram Image Bot 是一个基于 C++ 的 Telegram 机器人项目，可以处理用户发送给机器人的图片，并返回一个可公开访问的 URL。

## 功能特性

- **接收和处理用户发送的图片**：用户可以直接将图片发送给机器人，机器人会返回一个可分享的 URL。
- **处理群聊中的图片**：当机器人被 @ 并且是对图片的回复时，机器人会返回该图片的 URL。
- **动态线程池**：自动调整线程池大小以优化性能。
- **持久化状态**：自动保存并恢复 `offset`，以避免处理重复的消息。
- **错误处理**：友好的错误提示和详细的日志记录，便于调试和维护。

## 项目结构

```
/telegram_bot
├── include
│   ├── bot.h                # Telegram Bot 功能
│   ├── http_client.h        # HTTP 请求处理
│   ├── thread_pool.h        # 线程池管理
│   ├── utils.h              # 辅助功能
├── src
│   ├── bot.cpp              # Telegram Bot 实现
│   ├── http_client.cpp      # HTTP 请求实现
│   ├── main.cpp             # 主程序入口
│   ├── thread_pool.cpp      # 线程池实现
│   ├── utils.cpp            # 辅助功能实现
└── Makefile                 # 编译和清理脚本
```

## 环境要求

- **C++11/14/17**：支持 C++11 或以上版本的编译器。
- **libcurl**：用于处理 HTTP 请求。
- **nlohmann/json**：用于解析 JSON 数据。
- **POSIX**：线程池依赖于 POSIX 线程。

## 安装与使用

### 1. 克隆项目

```bash
git https://github.com/cryptoli/lite-telegram-image-bot.git
cd lite-telegram-image-bot
```

### 2. 安装依赖库

在 Ubuntu 上，你可以使用以下命令安装所需的库：

```bash
sudo apt-get install g++ libcurl4-openssl-dev make nlohmann-json3-dev
```

### 3. 编译项目

在项目根目录下运行：

```bash
make
```

### 4. 运行机器人

运行程序并传入 Telegram Bot API Token：

```bash
./telegram_bot <Your_Telegram_Bot_Token>
```

你可以通过 `@BotFather` 在 Telegram 中创建并获取你的 Bot Token。

## 配置说明

在 `bot.cpp` 文件中，你可以根据需要调整机器人的配置，例如：

- **处理的文件类型**：当前只处理图片（`photo`），你可以扩展到处理其他文件类型。
- **API 请求频率**：默认情况下，每秒请求一次更新，可以根据需要调整请求频率。

## 贡献

欢迎提交 issue 或 pull request 来帮助改进此项目。如果你有新的想法或发现了 bug，欢迎与我们分享。

## 许可证

该项目使用 [MIT License](LICENSE) 进行开源，详见 [LICENSE](LICENSE) 文件。

## 联系方式

如果你有任何问题或建议，欢迎通过以下方式联系：

- **GitHub Issues**：在项目仓库中提交问题
- **Telegram**：https://t.me/r857857
