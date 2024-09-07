FROM alpine:3.17

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.tuna.tsinghua.edu.cn/g' /etc/apk/repositories

RUN apk --no-cache add \
    g++ \
    make \
    curl \
    libcurl \
    curl-dev \
    openssl \
    libssl1.1 \
    sqlite-libs \
    sqlite-dev \
    libstdc++ \
    libgcc \
    supervisor \
    && rm -rf /var/cache/apk/*

RUN curl -fsSL "https://caddyserver.com/api/download?os=linux&arch=amd64" -o /usr/bin/caddy && chmod +x /usr/bin/caddy
RUN mkdir -p /app/include/nlohmann && \
    curl -L https://github.com/nlohmann/json/releases/download/v3.10.5/json.hpp -o /app/include/nlohmann/json.hpp


WORKDIR /app

COPY . /app
COPY supervisord.conf /etc/supervisord.conf

RUN make

EXPOSE 443

# 设置启动命令，启动Caddy和Telegram Bot
CMD ["/usr/bin/supervisord", "-c", "/etc/supervisord.conf"]
