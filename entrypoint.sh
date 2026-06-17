#!/bin/bash
set -e

export PATH="/opt/venv/bin:$PATH"

echo "[EMPI] Starting Xvfb on :99..."
Xvfb :99 -screen 0 1920x1080x24 -ac +extension GLX +render -noreset +extension RANDR &
sleep 1

echo "[EMPI] Starting VNC server..."
x11vnc -display :99 -forever -nopw -quiet -listen 0.0.0.0 -xkb -noxdamage -noxfixes -noscrollcopyrect -nomodtweak -scale 1 &
sleep 1

echo "[EMPI] Starting noVNC on port 8080..."
/usr/share/novnc/utils/novnc_proxy --listen 8080 --vnc localhost:5900 &

export QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu --use-gl=swiftshader"
#echo "[EMPI] Starting fluxbox..."
#fluxbox -display :99 &
#export DISPLAY=:99
#sleep 1
#openbox --replace &
echo "[EMPI] Launching empi_gui..."
export QT_DEBUG_PLUGINS=1
cd /app/build
./empi_gui
echo "Exit code: $?"

