#!/bin/bash
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --build build --target install

killall dde-grand-search-daemon && dde-grand-search-daemon &