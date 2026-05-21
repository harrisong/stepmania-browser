#!/bin/bash
# StepMania Browser - YouTube Import Companion Server
# Run this locally while playing on the GitHub Pages site.
# Usage: ./start.sh [port]   (default: 8765)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-8765}"

if [ ! -d "$DIR/.venv" ]; then
    echo "Setting up Python environment..."
    python3 -m venv "$DIR/.venv"
    "$DIR/.venv/bin/pip" install -q yt-dlp mutagen "numpy>=2.0" librosa
fi

echo "StepMania Companion Server: http://localhost:$PORT"
echo "  The game page will connect here to import YouTube songs."
PYTHONPATH="" exec "$DIR/.venv/bin/python3" "$DIR/server.py" "$PORT"
