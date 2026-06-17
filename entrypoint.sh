#!/bin/bash
set -e

export PATH="/opt/venv/bin:$PATH"

echo "[EMPI] Starting Xvfb on :99..."
Xvfb :99 -screen 0 1280x1024x24 -ac +extension RANDR &
sleep 1

echo "[EMPI] Starting VNC server..."
x11vnc -display :99 -forever -nopw -quiet -listen 0.0.0.0 -xkb &
sleep 1

echo "[EMPI] Starting noVNC on port 8080..."
/usr/share/novnc/utils/novnc_proxy --listen 8080 --vnc localhost:5900 &

echo "[EMPI] Launching empi_gui..."
cd /app/build/main
./empi_gui
