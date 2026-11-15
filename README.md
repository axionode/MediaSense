# MediaSense

Быстрый просмотрщик фото и видео для Windows 10 и 11: поворот с записью в файл, обрезка, конвертация, сортировка по папкам.
C++17 на Win32, Direct2D, DirectWrite, WIC и Media Foundation, без сторонних библиотек. Программа не выходит в сеть.

- Сборка: `build.cmd` на Windows или `build.sh` на Linux/macOS (нужен Python 3 и `pip install ziglang`).
- Конвертация видео и запись поворота в видеофайл требуют `tools\ffmpeg.exe` рядом с `MediaSense.exe` (в репозиторий не входит).
- Установка, управление и форматы описаны в [README.txt](README.txt), настройка кодеков в [AGENT_SETUP.md](AGENT_SETUP.md).
