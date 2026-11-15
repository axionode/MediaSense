#!/bin/sh
# Cross-build from Linux/macOS: pip install ziglang, then ./build.sh
cd "$(dirname "$0")" && mkdir -p build
python3 -m ziglang c++ -target x86_64-windows-gnu -std=c++17 -O2 -municode -mwindows -Wl,--subsystem,windows -Wno-unknown-pragmas \
  src/main.cpp src/app.rc -o build/MediaSense.exe \
  -ld2d1 -ldwrite -ld3d11 -ldxgi -lwindowscodecs -lmfplat -lmf -lole32 -loleaut32 -lshell32 -lshlwapi -luser32 -lgdi32 -ldwmapi -ladvapi32 -s
