FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies (no GUI, no X11, no VNC)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    ca-certificates \
    qt6-base-dev \
    libpoppler-qt6-dev \
    nlohmann-json3-dev \
    python3 \
    python3-pip \
    python3-venv \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Python dependencies
COPY requirements.txt /tmp/requirements.txt
RUN python3 -m venv /opt/venv && \
    /opt/venv/bin/pip install --no-cache-dir -r /tmp/requirements.txt && \
    /opt/venv/bin/python -m spacy download en_core_web_sm

ENV PATH="/opt/venv/bin:$PATH"

WORKDIR /app

# Copy source
COPY CMakeLists.txt .
COPY cmake/ cmake/
COPY src/ src/
COPY gui/ gui/
COPY integrations/ integrations/
COPY config/ config/

# Build HTTP server
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_HTTP=ON \
    -DBUILD_GUI=OFF \
    -DBUILD_TESTS=OFF \
    -DCMAKE_CXX_FLAGS="-DEMPI_NO_LLAMA" \
    -G Ninja \
    && cmake --build build --target empi_http -- -j$(nproc)

# Copy web interface
RUN cp -r gui/web build/gui/

EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/api/health || exit 1

# Run
CMD ["/app/build/empi_http", "-c", "/app/config/agent_config.toml", "-p", "8080"]
