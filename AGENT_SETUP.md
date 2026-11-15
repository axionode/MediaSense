# MediaSense: инструкция для ИИ-агента

Цель: чтобы MediaSense на этом компьютере открывал и конвертировал все форматы. Программа сама ничего не скачивает,
установку делаешь ты или пользователь.

## 1. Проверить, что уже есть

Запусти `MediaSense.exe`, нажми `I`. Окно "Компоненты" покажет статус каждого пункта.
Кнопка "Скопировать инструкцию для ИИ-агента" кладёт в буфер текущие статусы и команды.

Проверка без интерфейса (PowerShell):

```powershell
Get-AppxPackage | Where-Object Name -match 'HEIF|HEVC|Webp|AV1|VP9|RawImage|MPEG2' | Select-Object Name, Version
Test-Path "$PSScriptRoot\tools\ffmpeg.exe"
```

## 2. Доустановить кодеки Windows (нужен winget и вход в Microsoft Store)

```powershell
$a = '--source','msstore','--accept-package-agreements','--accept-source-agreements'
winget install --id 9PMMSR1CGPWG @a   # HEIF Image Extensions (HEIC)
winget install --id 9N4WGH0Z6VHQ @a   # HEVC Video Extensions from Device Manufacturer (бесплатный)
winget install --id 9PG2DK419DRG @a   # Webp Image Extensions
winget install --id 9MVZQVXJBQ9V @a   # AV1 Video Extension (нужен и для AVIF)
winget install --id 9N4D0MSMP0PT @a   # VP9 Video Extensions
winget install --id 9NCTDW2W1BH8 @a   # Raw Image Extension
winget install --id 9N95Q1ZZPMH4 @a   # MPEG-2 Video Extension
```

Если бесплатный HEVC недоступен в регионе, платный вариант: `winget install --id 9NMZLZ57R3T7 --source msstore`.
Идентификаторы пакетов сверяй с Microsoft Store по названию, если winget пакет не находит.

## 3. FFmpeg

Нужен файл `tools\ffmpeg.exe` рядом с `MediaSense.exe` (или ffmpeg в PATH).
Подходит любая сборка win64 с libx264: github.com/BtbN/FFmpeg-Builds (файл `ffmpeg-n8.1-latest-win64-gpl-8.1.zip`, из архива нужен только `bin\ffmpeg.exe`).

## 4. Установка программы

`Install.cmd` ставит программу в `%LOCALAPPDATA%\Programs\MediaSense`, создаёт ярлыки и регистрирует типы файлов в HKCU.
Права администратора не нужны. Ассоциации по умолчанию пользователь выбирает сам через "Открыть с помощью".

## 5. Проверка результата

`SelfTest.cmd` запускает сценарий на папке `test_media`: галерея, фото, поворот, кроп, экспорт, видео, конвертация.
Результат: `test_media\selftest.log` и снимки `test_media\shot_*.jpg`, экспортированные файлы в `test_media\out`.
Признаки успеха в логе: `photo1: bmp=1`, `phone clip: ready=1 error=0 frame=1`, строки `Сохранено:` для экспорта, `selftest done`.

## Где что хранится

- `%LOCALAPPDATA%\MediaSense\rotations.txt` повороты, строка вида `1|c:\путь\файл.jpg` (1 = 90 градусов по часовой, 2 = 180, 3 = 270)
- `%LOCALAPPDATA%\MediaSense\settings.txt` папка экспорта, выбранные пресеты, громкость, положение окна
