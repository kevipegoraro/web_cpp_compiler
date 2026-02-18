#!/bin/bash
set -euo pipefail

if [ ! -f "server.cpp" ]; then
  echo "server.cpp missing!"
  exit 1
fi

mkdir -p bin

echo "Compiling server..."
g++ server.cpp -o bin/server -std=c++17 -O2 -Wall -Wextra -pedantic
echo "✔ Built: bin/server"
