FROM alpine:3.17 AS build

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.tuna.tsinghua.edu.cn/g' /etc/apk/repositories

# 安装编译所需的依赖
RUN apk --no-cache add \
    g++ \
    make \
    curl \
    curl-dev \
    openssl-dev \
    sqlite-dev \
    libstdc++ \
    libgcc

# 创建工作目录并下载 json 库
RUN mkdir -p /app/include/nlohmann && \
    wget -qO /app/include/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.10.5/json.hpp

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . .

# 编译项目
RUN make

FROM alpine:3.17

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.tuna.tsinghua.edu.cn/g' /etc/apk/repositories

RUN apk --no-cache add \
    libcurl \
    libssl1.1 \
    sqlite-libs \
    libstdc++ \
    supervisor \
    && rm -rf /var/cache/apk/*

# 安装 Caddy
RUN wget -qO /usr/bin/caddy "https://caddyserver.com/api/download?os=linux&arch=amd64" && chmod +x /usr/bin/caddy

WORKDIR /app
COPY --from=build /app/telegram_bot /app/telegram_bot
COPY supervisord.conf /etc/supervisord.conf

EXPOSE 443

CMD ["/usr/bin/supervisord", "-c", "/etc/supervisord.conf"]