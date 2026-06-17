FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    DISPLAY=:99 \
    QT_QPA_PLATFORM=offscreen

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    ca-certificates \
    qt6-base-dev \
    qt6-webengine-dev \
    qt6-webchannel-dev \
    qt6-base-dev-tools \
    libpoppler-qt6-dev \
    nlohmann-json3-dev \
    python3 \
    python3-pip \
    python3-venv \
    xvfb \
    x11vnc \
    novnc \
    fonts-noto-core \
    fonts-noto-cjk \
    fonts-noto-color-emoji \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt /tmp/requirements.txt
RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --no-cache-dir -r /tmp/requirements.txt
ENV PATH="/opt/venv/bin:$PATH"

WORKDIR /app

COPY CMakeLists.txt .
COPY cmake/ cmake/
COPY src/ src/
COPY gui/ gui/
COPY integrations/ integrations/
COPY config/ config/

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_GUI=ON \
    -DBUILD_TESTS=OFF \
    -G Ninja \
    && cmake --build build --target empi_gui -- -j$(nproc)

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 8080

ENTRYPOINT ["/entrypoint.sh"]
