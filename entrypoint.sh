#!/bin/bash
set -e

echo "[EMPI] Starting HTTP server..."

# Check config
if [ ! -f "/app/config/agent_config.toml" ]; then
    echo "[EMPI] Warning: Config file not found, using defaults"
fi

# Run HTTP server
cd /app/build
exec ./empi_http -c /app/config/agent_config.toml -p 8080
