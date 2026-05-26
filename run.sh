#!/bin/bash
set -e

echo "============================================"
echo " EMPI Agent Framework — Setup & Build"
echo "============================================"

# ---------- 1. Системные зависимости ----------
echo ""
echo "[1/5] Checking system dependencies..."

# Python3
if ! command -v python3 &> /dev/null; then
    echo "  -> Installing python3..."
    sudo apt-get update && sudo apt-get install -y python3 python3-pip python3-venv
else
    echo "  -> python3: $(python3 --version)"
fi

# Node.js
if ! command -v node &> /dev/null; then
    echo "  -> Installing nodejs..."
    sudo apt-get install -y nodejs npm
else
    echo "  -> node: $(node --version)"
fi

# C++ build tools
if ! command -v cmake &> /dev/null; then
    echo "  -> Installing cmake & build-essential..."
    sudo apt-get install -y cmake build-essential
else
    echo "  -> cmake: $(cmake --version | head -1)"
fi

# ---------- 2. Python-окружение ----------
echo ""
echo "[2/5] Setting up Python environment..."

python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
python3 -m spacy download en_core_web_sm

echo "  -> Python deps installed"

# ---------- 3. Node.js зависимости ----------
echo ""
echo "[3/5] Installing Node.js dependencies..."
npm install
echo "  -> Node deps installed"

# ---------- 4. Сборка C++ ----------
echo ""
echo "[4/5] Building C++ project..."

rm -rf build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..
echo "  -> C++ build complete"

# ---------- 5. Проверка ----------
echo ""
echo "[5/5] Running quick check..."

# Проверяем бинарники
if [ -f build/orchestrate_agents ]; then
    echo "  -> orchestrate_agents: OK"
else
    echo "  -> orchestrate_agents: NOT FOUND — check build output"
fi

# Проверяем Python-скрипты
source .venv/bin/activate
if python3 -c "from text_analyzer import TextAnalyzer" 2>/dev/null; then
    echo "  -> text_analyzer.py: OK"
else
    echo "  -> text_analyzer.py: WARNING — check integrations/"
fi

# ---------- Готово ----------
echo ""
echo "============================================"
echo " Setup complete!"
echo ""
echo " Next steps:"
echo "  1. Edit config/agent_config.toml — add API keys"
echo "  2. Run: ./build/orchestrate_agents"
echo "  3. Or build GUI: cd build && cmake .. -DBUILD_GUI=ON && make"
echo "============================================"
