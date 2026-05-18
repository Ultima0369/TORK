# ── TORK Docker 镜像 ──────────────────────────────────────────
#   自进化硅基生命体 · 容器化部署
#   构建: docker build -t tork .
#   运行: docker compose up
# ──────────────────────────────────────────────────────────────

FROM ubuntu:22.04 AS builder

LABEL maintainer="TORK Team"
LABEL description="TORK - Self-Evolving Silicon Lifeform"
LABEL version="0.9.0"

# 避免交互式配置
ENV DEBIAN_FRONTEND=noninteractive

# 构建依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    g++ \
    make \
    nasm \
    libc6-dev \
    ca-certificates \
    python3 \
    python3-pip \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tork

# 先复制构建系统（利用 docker layer 缓存）
COPY Makefile mk/ ./
COPY src/config.h src/

# 复制源码
COPY src/ ./src/

# 编译
RUN mkdir -p build persist && \
    make -j$(nproc) && \
    cp build/tork_engine /usr/local/bin/torkd

# ── 运行阶段 ──────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6 \
    ca-certificates \
    python3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/bin/torkd /usr/local/bin/torkd
COPY --from=builder /tork/persist /tork/persist

WORKDIR /tork

EXPOSE 42069/udp  # 分布式多播
EXPOSE 9876       # SP 桥接

VOLUME ["/tork/persist"]

HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD ls /tmp/torkd.sock 2>/dev/null && echo "alive" || exit 1

ENTRYPOINT ["torkd"]
CMD ["--foreground"]
