#!/bin/bash
set -e

echo "[EMPI] Starting HTTP server..."

# Enable Qt logging to stdout
export QT_LOGGING_RULES="*.debug=true"
export QT_MESSAGE_PATTERN="%{time} [%{type}] %{message}"

# Check config
if [ ! -f "/app/config/agent_config.toml" ]; then
    echo "[EMPI] Warning: Config file not found, using defaults"
fi

# Run HTTP server with output to stdout
cd /app/build
exec ./empi_http -c /app/config/agent_config.toml -p 8080 2>&1
