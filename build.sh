#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
cmake -S . -B build
cmake --build build
echo
echo "Built. Run: ./build/securetrace"
echo "Help:  ./build/securetrace --help"
echo "Demo:  ./build/securetrace --demo"
