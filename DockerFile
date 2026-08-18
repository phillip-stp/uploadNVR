# ==========================================
# STAGE 1: Builder
# ==========================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Added libyaml-cpp-dev for the config.yaml parser
RUN apt-get update && apt-get install -y \
    build-essential cmake git pkg-config python3 curl gnupg \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libsodium-dev \
    libyaml-cpp-dev

# Mandatory Google Coral Edge TPU development package
RUN echo "deb https://packages.cloud.google.com/apt coral-edgetpu-stable main" | tee /etc/apt/sources.list.d/coral-edgetpu.list && \
    curl -sL https://packages.cloud.google.com/apt/doc/apt-key.gpg | gpg --dearmor > /etc/apt/trusted.gpg.d/coral-edgetpu.gpg && \
    apt-get update && \
    apt-get install -y libedgetpu-dev

WORKDIR /app
COPY . .

# Fetch TensorFlow and Build
RUN rm -rf dependencies/tensorflow && \
    git clone --depth 1 --branch v2.21.0 https://github.com/tensorflow/tensorflow.git dependencies/tensorflow

RUN cmake -B build -S . && \
    cmake --build build -j$(nproc)

# ==========================================
# STAGE 2: Runtime
# ==========================================
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install mandatory runtime libraries
# Added rclone for Google Drive syncing
RUN apt-get update && apt-get install -y \
    ffmpeg libsodium23 curl gnupg udev rclone && \
    echo "deb https://packages.cloud.google.com/apt coral-edgetpu-stable main" | tee /etc/apt/sources.list.d/coral-edgetpu.list && \
    curl -sL https://packages.cloud.google.com/apt/doc/apt-key.gpg | gpg --dearmor > /etc/apt/trusted.gpg.d/coral-edgetpu.gpg && \
    apt-get update && \
    apt-get install -y libedgetpu1-std && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/uploadNVR .

CMD ["./uploadNVR"]