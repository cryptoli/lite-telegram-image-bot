# 使用 Ubuntu 基础镜像
FROM ubuntu:22.04 AS build

# 更新并安装所需依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    curl \
    libcurl4-openssl-dev \
    libssl-dev \
    libsqlite3-dev \
    wget \
    supervisor \
    && rm -rf /var/lib/apt/lists/*

# 创建工作目录并下载 nlohmann JSON 库
RUN mkdir -p /app/include/nlohmann && \
    wget -qO /app/include/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.10.5/json.hpp

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . .

# 编译项目
RUN make

# 构建最终镜像，基于 Ubuntu 最小安装
FROM ubuntu:22.04

# 安装运行时依赖
RUN apt-get update && apt-get install -y \
    libcurl4 \
    libssl3 \
    libsqlite3-0 \
    supervisor \
    wget \
    && rm -rf /var/lib/apt/lists/*

# 安装 Caddy
RUN wget -qO /usr/bin/caddy "https://caddyserver.com/api/download?os=linux&arch=amd64" && chmod +x /usr/bin/caddy

# 设置工作目录并复制构建生成的文件
WORKDIR /app
COPY --from=build /app/telegram_bot /app/telegram_bot
COPY supervisord.conf /etc/supervisord.conf
COPY config.json /app/config.json
COPY Caddyfile /app/Caddyfile

# 暴露端口
EXPOSE 443

# 设置启动命令，启动 Caddy 和 Telegram Bot
CMD ["/usr/bin/supervisord", "-c", "/etc/supervisord.conf"]
