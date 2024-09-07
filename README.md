# Telegram Image Bot

Lite Telegram Image Bot 是一个基于 C++ 的 Telegram 机器人项目，可以处理用户发送给机器人的图片，并返回一个可公开访问的 URL。

## 功能特性

- **接收和处理用户发送的图片**：用户可以直接将图片发送给机器人，机器人会返回一个可分享的 URL。
- **处理群聊中的图片**：当机器人被 @ 并且是对图片的回复时，机器人会返回该图片的 URL。
- **动态线程池**：自动调整线程池大小以优化性能。
- **错误处理**：友好的错误提示和详细的日志记录，便于调试和维护。

## 项目结构

```
lite-telegram-image-bot/
├── Caddyfile
├── Makefile
├── README.md
├── config.json
├── include
│   ├── bot.h
│   ├── config.h
│   ├── db_manager.h
│   ├── http_client.h
│   ├── httplib.h
│   ├── image_cache_manager.h
│   ├── request_handler.h
│   ├── server.h
│   ├── thread_pool.h
│   ├── thread_pool.tpp
│   └── utils.h
├── src
│   ├── bot.cpp
│   ├── config.cpp
│   ├── db_manager.cpp
│   ├── http_client.cpp
│   ├── main.cpp
│   ├── request_handler.cpp
│   ├── server.cpp
│   ├── thread_pool.cpp
│   └── utils.cpp
└── templates  暂未使用
    ├── index.html
    ├── login.html
    └── register.html
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
sudo apt-get install g++ libcurl4-openssl-dev make nlohmann-json3-dev libssl-dev sqlite3 libsqlite3-dev
```

### 3. 编译项目

在项目根目录下运行：

```bash
make
```
### 4. 修改配置文件config.json
当前支持两种开启ssl/tls的方式
1. hostname设置为解析到当前ip的域名，port为443，use_https设为true，正确设置ssl证书，webhook_url为当前域名即可不借用其他软件开启ssl/tls
2. hostname设置为127.0.0.1，port为除了443外的端口，use_https设为false，不必填写ssl证书相关信息，可使用Caddy进行反代，webhook_url为caddy反代域名
其他参数解释：
api_token为botfather申请的bot api
secret_token为随机字符串，可保证webhook接口安全
owner_id为自己的telegram id，即管理bot的telegram账户id，可通过 @userinfobot 机器人获取,
telegram_api_url默认为官方api，如果需要自定义可以修改
```bash
{
    "server": {
        "hostname": "127.0.0.1",
        "port": 8080,
        "use_https": false,
        "ssl_certificate": "path/to/your/certificate.crt",
        "ssl_key": "path/to/your/private.key",
        "allow_registration": true,
        "webhook_url": "https://yourdomain.com"
    },
    "api_token": "your_telegram_api_token",
    "secret_token": "random_secret_token",
    "owner_id": "your_telegram_id",
    "telegram_api_url": "https://api.telegram.org",
    "mime_types": {
        ".jpg": "image/jpeg",
        ".jpeg": "image/jpeg",
        ".png": "image/png",
        ".gif": "image/gif",
        ".bmp": "image/bmp",
        ".tiff": "image/tiff",
        ".webp": "image/webp",
        ".mp4": "video/mp4",
        ".mp3": "audio/mpeg",
        ".ogg": "audio/ogg",
        ".wav": "audio/wav",
        ".m4a": "audio/mp4",
        ".aac": "audio/aac",
        ".pdf": "application/pdf",
        ".doc": "application/msword",
        ".docx": "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        ".xls": "application/vnd.ms-excel",
        ".xlsx": "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        ".ppt": "application/vnd.ms-powerpoint",
        ".pptx": "application/vnd.openxmlformats-officedocument.presentationml.presentation",
        ".zip": "application/zip",
        ".rar": "application/x-rar-compressed",
        ".7z": "application/x-7z-compressed",
        ".txt": "text/plain",
        ".csv": "text/csv",
        ".json": "application/json",
        ".xml": "application/xml",
        ".html": "text/html",
        ".htm": "text/html",
        ".css": "text/css",
        ".js": "application/javascript",
        ".webm": "video/webm",
        ".mkv": "video/x-matroska",
        ".mov": "video/quicktime",
        ".avi": "video/x-msvideo",
        ".flv": "video/x-flv",
        ".apk": "application/vnd.android.package-archive",
        ".tar": "application/x-tar",
        ".gz": "application/gzip",
        ".bz2": "application/x-bzip2",
        ".stl": "application/vnd.ms-pkistl"
    },
    "cache": {
        "max_size_mb": 100,
        "max_age_seconds": 3600
    }
}
```
### 5. 生成证书（若不使用其他反代工具）
证书放在项目根目录下，名称分别为server.key、server.crt
```bash
sudo apt-get update
sudo apt-get install certbot
sudo certbot certonly --standalone -d yourdomain.com

sudo cp /etc/letsencrypt/live/yourdomain.com/privkey.pem /path/to/your/project/server.key
sudo cp /etc/letsencrypt/live/yourdomain.com/fullchain.pem /path/to/your/project/server.crt

```
### 6. 运行机器人

```bash
./telegram_bot
```

你可以通过 `@BotFather` 在 Telegram 中创建并获取你的 Bot Token。
### 7. 构建docker镜像
```bash
# 从仓库拉取
docker push ljh123/telegram-bot:latest
# OR 自己构建
docker build -t lite-telegram-image-bot .
```
### 8. docker启动
WEBHOOK_URL=https://your_domain.com 如果你已经有其他应用可以采用其他路径例如：WEBHOOK_URL=https://your_domain.com/example ，可以反向代理这个地址，端口改成其他的
SECRET_TOKEN一定要保持足够长度且随机
```bash
docker run -d \
    -e DOMAIN=your_domain.com \
    -e WEBHOOK_URL=https://your_domain.com \
    -e API_TOKEN=your_telegram_bot_token \
    -e SECRET_TOKEN=your_secret_token \
    -e OWNER_ID=your_owner_id \
    -e TELEGRAM_API_URL=https://api.telegram.org \
    -p 443:443 \
    my-bot-with-caddy

```
### 9. 机器人命令
可将下面的命令发送给botfather
```bash
collect - 收集并保存回复中的文件
remove - 删除已收集文件（仍然可以访问）
my - 列出当前用户收集的文件
ban - 封禁用户（仅限拥有者）
openregister - 开启获取文件URL功能（仅限拥有者）
closeregister - 关闭获取文件URL功能（仅限拥有者）
```
## 贡献

欢迎提交 issue 或 pull request 来帮助改进此项目。如果你有新的想法或发现了 bug，欢迎与我们分享。

## 许可证

该项目使用 [MIT License](LICENSE) 进行开源，详见 [LICENSE](LICENSE) 文件。

## 联系方式

如果你有任何问题或建议，欢迎通过以下方式联系：

- **GitHub Issues**：在项目仓库中提交问题
- **Telegram**：https://t.me/r857857
