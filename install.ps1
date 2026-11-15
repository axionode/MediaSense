# Установка MediaSense для текущего пользователя. Права администратора и интернет не нужны.
$ErrorActionPreference = 'Stop'
$src = Split-Path -Parent $MyInvocation.MyCommand.Path
$dst = Join-Path $env:LOCALAPPDATA 'Programs\MediaSense'
$exe = Join-Path $dst 'MediaSense.exe'
if (-not (Test-Path (Join-Path $src 'MediaSense.exe'))) { Write-Host 'Не найден MediaSense.exe рядом с установщиком.' -ForegroundColor Red; exit 1 }

Get-Process MediaSense -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dst | Out-Null
New-Item -ItemType Directory -Force (Join-Path $dst 'tools') | Out-Null
foreach ($f in 'MediaSense.exe','README.txt','AGENT_SETUP.md','Uninstall.cmd','uninstall.ps1') {
    if (Test-Path (Join-Path $src $f)) { Copy-Item (Join-Path $src $f) $dst -Force }
}
if (Test-Path (Join-Path $src 'tools\ffmpeg.exe')) {
    Write-Host 'Копирую ffmpeg (около 160 МБ)...'
    Copy-Item (Join-Path $src 'tools\*') (Join-Path $dst 'tools') -Force
} else { Write-Host 'ffmpeg.exe не найден в папке tools: просмотр будет работать, конвертация видео нет.' -ForegroundColor Yellow }

# Ярлыки
$ws = New-Object -ComObject WScript.Shell
foreach ($p in (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\MediaSense.lnk'), (Join-Path ([Environment]::GetFolderPath('Desktop')) 'MediaSense.lnk')) {
    $l = $ws.CreateShortcut($p); $l.TargetPath = $exe; $l.WorkingDirectory = $dst; $l.IconLocation = "$exe,0"; $l.Save()
}

# Регистрация в "Открыть с помощью" и в "Приложения по умолчанию"
$cls = 'HKCU:\Software\Classes'
$cmd = '"' + $exe + '" "%1"'
New-Item -Force "$cls\MediaSense.Media\shell\open\command" | Out-Null
Set-ItemProperty "$cls\MediaSense.Media" '(default)' 'Медиафайл MediaSense'
New-Item -Force "$cls\MediaSense.Media\DefaultIcon" | Out-Null
Set-ItemProperty "$cls\MediaSense.Media\DefaultIcon" '(default)' "$exe,0"
Set-ItemProperty "$cls\MediaSense.Media\shell\open\command" '(default)' $cmd
New-Item -Force "$cls\Applications\MediaSense.exe\shell\open\command" | Out-Null
Set-ItemProperty "$cls\Applications\MediaSense.exe\shell\open\command" '(default)' $cmd
Set-ItemProperty "$cls\Applications\MediaSense.exe" 'FriendlyAppName' 'MediaSense'
New-Item -Force "$cls\Applications\MediaSense.exe\SupportedTypes" | Out-Null
New-Item -Force 'HKCU:\Software\MediaSense\Capabilities\FileAssociations' | Out-Null
Set-ItemProperty 'HKCU:\Software\MediaSense\Capabilities' 'ApplicationName' 'MediaSense'
Set-ItemProperty 'HKCU:\Software\MediaSense\Capabilities' 'ApplicationDescription' 'Быстрый просмотр и конвертация фото и видео'
$exts = @('.jpg','.jpeg','.jpe','.jfif','.png','.bmp','.dib','.gif','.tif','.tiff','.ico','.webp','.heic','.heif','.hif','.avif','.jxr','.wdp','.hdp','.dds','.jxl','.cr2','.cr3','.crw','.nef','.nrw','.arw','.srf','.sr2','.dng','.orf','.rw2','.raf','.pef','.srw','.erf','.kdc','.mrw','.3fr','.x3f','.tga','.psd','.exr','.hdr','.pcx','.ppm','.pgm','.pbm','.pnm','.jp2','.j2k','.qoi','.sgi','.xbm','.mp4','.m4v','.mov','.mkv','.avi','.wmv','.asf','.webm','.3gp','.3g2','.mts','.m2ts','.ts','.mpg','.mpeg','.vob','.flv','.ogv','.divx','.f4v','.mxf','.m2v')
foreach ($e in $exts) {
    New-Item -Force "$cls\$e\OpenWithProgids" | Out-Null
    New-ItemProperty -Force -Path "$cls\$e\OpenWithProgids" -Name 'MediaSense.Media' -PropertyType String -Value '' | Out-Null
    New-ItemProperty -Force -Path "$cls\Applications\MediaSense.exe\SupportedTypes" -Name $e -PropertyType String -Value '' | Out-Null
    New-ItemProperty -Force -Path 'HKCU:\Software\MediaSense\Capabilities\FileAssociations' -Name $e -PropertyType String -Value 'MediaSense.Media' | Out-Null
    # Прямой пункт в контекстном меню файла, в верхнем блоке
    $k = "$cls\SystemFileAssociations\$e\shell\MediaSense"
    New-Item -Force "$k\command" | Out-Null
    Set-ItemProperty $k '(default)' 'Открыть в MediaSense'
    Set-ItemProperty $k 'Icon' "$exe,0"
    Set-ItemProperty $k 'Position' 'Top'
    Set-ItemProperty "$k\command" '(default)' $cmd
}
New-Item -Force 'HKCU:\Software\RegisteredApplications' | Out-Null
Set-ItemProperty 'HKCU:\Software\RegisteredApplications' 'MediaSense' 'Software\MediaSense\Capabilities'
# Пункт в контекстном меню папок
New-Item -Force "$cls\Directory\shell\MediaSense\command" | Out-Null
Set-ItemProperty "$cls\Directory\shell\MediaSense" '(default)' 'Открыть в MediaSense'
Set-ItemProperty "$cls\Directory\shell\MediaSense" 'Icon' "$exe,0"
Set-ItemProperty "$cls\Directory\shell\MediaSense\command" '(default)' $cmd
# Запись в "Установка и удаление программ"
$un = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaSense'
New-Item -Force $un | Out-Null
Set-ItemProperty $un 'DisplayName' 'MediaSense'
Set-ItemProperty $un 'DisplayVersion' '1.0.0'
Set-ItemProperty $un 'DisplayIcon' "$exe,0"
Set-ItemProperty $un 'InstallLocation' $dst
Set-ItemProperty $un 'UninstallString' ('cmd.exe /c "' + (Join-Path $dst 'Uninstall.cmd') + '"')
New-ItemProperty -Force -Path $un -Name 'NoModify' -PropertyType DWord -Value 1 | Out-Null
New-ItemProperty -Force -Path $un -Name 'NoRepair' -PropertyType DWord -Value 1 | Out-Null

Write-Host ''
Write-Host "Готово. MediaSense установлен в $dst" -ForegroundColor Green
Write-Host 'Ярлыки: рабочий стол и меню Пуск.'
Write-Host 'В меню правой кнопки по фото и видео появился пункт "Открыть в MediaSense".'
Write-Host 'Чтобы открывать файлы двойным кликом: правый клик по файлу, "Открыть с помощью", "Выбрать другое приложение", MediaSense, "Всегда".'
