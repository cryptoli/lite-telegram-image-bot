# Stage 1: Build stage
FROM alpine:3.17 AS build

# 使用阿里云的源加速下载
RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.aliyun.com/g' /etc/apk/repositories

# 安装编译所需的依赖，包括静态链接所需的开发库
RUN apk --no-cache add \
    g++ \
    make \
    musl-dev \
    curl-static \
    openssl-libs-static \
    sqlite-static \
    libstdc++ \
    libgcc

# 创建工作目录并下载 json 库
RUN mkdir -p /app/include/nlohmann && \
    wget -qO /app/include/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.10.5/json.hpp

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . .

# 编译项目，使用 musl 静态编译
RUN g++ -static -o telegram_bot src/*.cpp -Iinclude -lcurl -lssl -lcrypto -pthread -lsqlite3

# Stage 2: Final stage (runtime environment)
FROM alpine:3.17

# 使用阿里云的源加速下载
RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.aliyun.com/g' /etc/apk/repositories

# 安装运行时所需的最小依赖
RUN apk --no-cache add \
    libcurl \
    libssl1.1 \
    sqlite-libs \
    libstdc++ \
    supervisor \
    && rm -rf /var/cache/apk/*

# 安装 Caddy
RUN wget -qO /usr/bin/caddy "https://caddyserver.com/api/download?os=linux&arch=amd64" && chmod +x /usr/bin/caddy

# 设置工作目录
WORKDIR /app

# 从构建阶段复制生成的静态链接的可执行文件
COPY --from=build /app/telegram_bot /app/telegram_bot

# 复制 supervisord 配置文件
COPY supervisord.conf /etc/supervisord.conf

# 暴露端口
EXPOSE 443

# 设置启动命令，启动 Caddy 和 Telegram Bot
CMD ["/usr/bin/supervisord", "-c", "/etc/supervisord.conf"]