#!/bin/bash
set -euo pipefail

if [ ! -x "bin/server" ]; then
  echo "bin/server not found. Run ./build.sh first."
  exit 1
fi

echo "Running server..."
./bin/server
