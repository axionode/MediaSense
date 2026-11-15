# Удаление MediaSense. Сохранённые повороты и настройки (%LOCALAPPDATA%\MediaSense) остаются; удалить их: uninstall.ps1 -Purge
param([switch]$Purge)
$dst = Join-Path $env:LOCALAPPDATA 'Programs\MediaSense'
Get-Process MediaSense -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$cls = 'HKCU:\Software\Classes'
$exts = @('.jpg','.jpeg','.jpe','.jfif','.png','.bmp','.dib','.gif','.tif','.tiff','.ico','.webp','.heic','.heif','.hif','.avif','.jxr','.wdp','.hdp','.dds','.jxl','.cr2','.cr3','.crw','.nef','.nrw','.arw','.srf','.sr2','.dng','.orf','.rw2','.raf','.pef','.srw','.erf','.kdc','.mrw','.3fr','.x3f','.tga','.psd','.exr','.hdr','.pcx','.ppm','.pgm','.pbm','.pnm','.jp2','.j2k','.qoi','.sgi','.xbm','.mp4','.m4v','.mov','.mkv','.avi','.wmv','.asf','.webm','.3gp','.3g2','.mts','.m2ts','.ts','.mpg','.mpeg','.vob','.flv','.ogv','.divx','.f4v','.mxf','.m2v')
foreach ($e in $exts) {
    Remove-ItemProperty -Path "$cls\$e\OpenWithProgids" -Name 'MediaSense.Media' -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force "$cls\SystemFileAssociations\$e\shell\MediaSense" -ErrorAction SilentlyContinue
}
Remove-Item -Recurse -Force "$cls\MediaSense.Media","$cls\Applications\MediaSense.exe","$cls\Directory\shell\MediaSense",'HKCU:\Software\MediaSense','HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MediaSense' -ErrorAction SilentlyContinue
Remove-ItemProperty -Path 'HKCU:\Software\RegisteredApplications' -Name 'MediaSense' -ErrorAction SilentlyContinue
Remove-Item -Force (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\MediaSense.lnk'),(Join-Path ([Environment]::GetFolderPath('Desktop')) 'MediaSense.lnk') -ErrorAction SilentlyContinue
if ($Purge) { Remove-Item -Recurse -Force (Join-Path $env:LOCALAPPDATA 'MediaSense') -ErrorAction SilentlyContinue }
Set-Location $env:TEMP
Remove-Item -Recurse -Force $dst -ErrorAction SilentlyContinue
Write-Host 'MediaSense удалён.' -ForegroundColor Green
