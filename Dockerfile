FROM alpine:3.14

RUN apk --no-cache add \
    g++ \
    make \
    curl \
    libcurl \
    openssl \
    libssl1.1 \
    sqlite-libs \
    sqlite-dev \
    libstdc++ \
    libgcc \
    supervisor \
    caddy \
    && rm -rf /var/cache/apk/*

WORKDIR /app

COPY . /app
COPY supervisord.conf /etc/supervisord.conf

RUN make

EXPOSE 443

# 设置启动命令，启动Caddy和Telegram Bot
CMD ["/usr/bin/supervisord", "-c", "/etc/supervisord.conf"]
