@echo off
rem Rebuild MediaSense.exe on Windows. Needs Python 3 once: pip install ziglang (the only moment the network is used, and only for the compiler).
cd /d "%~dp0"
python -m ziglang version >nul 2>&1 || python -m pip install ziglang
python -m ziglang c++ -target x86_64-windows-gnu -std=c++17 -O2 -municode -mwindows -Wl,--subsystem,windows -Wno-unknown-pragmas src/main.cpp src/app.rc -o MediaSense.exe -ld2d1 -ldwrite -ld3d11 -ldxgi -lwindowscodecs -lmfplat -lmf -lole32 -loleaut32 -lshell32 -lshlwapi -luser32 -lgdi32 -ldwmapi -ladvapi32 -s
if errorlevel 1 (echo BUILD FAILED) else (echo OK: MediaSense.exe)
pause
