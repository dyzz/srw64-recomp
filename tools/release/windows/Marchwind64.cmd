@echo off
rem Marchwind64 for Windows: finds your ROM (Super Robot Taisen 64, Japan, Rev 0) and
rem starts the game, as marchwind64.sh does on Linux. Other options go to the program:
rem Marchwind64.exe --play --help lists them.
setlocal
chcp 65001 >nul
set "HERE=%~dp0"
set "DATA=%LOCALAPPDATA%\SRW64Recomp"
set "ROM="
if defined SRW64_ROM if exist "%SRW64_ROM%" set "ROM=%SRW64_ROM%"
if not defined ROM if exist "%DATA%\rom.z64" set "ROM=%DATA%\rom.z64"
if not defined ROM if exist "%HERE%rom.z64" set "ROM=%HERE%rom.z64"
rem Any byte order: the game turns a .v64 or .n64 dump into .z64 itself.
for %%E in (n64 v64) do (
  if not defined ROM if exist "%DATA%\rom.%%E" set "ROM=%DATA%\rom.%%E"
  if not defined ROM if exist "%HERE%rom.%%E" set "ROM=%HERE%rom.%%E"
)
if not defined ROM (
  powershell -NoProfile -Command "Add-Type -AssemblyName PresentationFramework; [System.Windows.MessageBox]::Show('找不到 ROM：请把超级机器人大战 64（日版 Rev 0）复制到 %DATA%\rom.z64，或放在 Marchwind64.cmd 旁边并命名为 rom.z64。' + [Environment]::NewLine + [Environment]::NewLine + 'No ROM found: copy your Super Robot Taisen 64 ROM (Japan, Rev 0) to %DATA%\rom.z64, or next to Marchwind64.cmd as rom.z64.', 'Marchwind64') | Out-Null"
  exit /b 1
)
rem First launch starts in Simplified Chinese; the settings window changes it.
set "LANG_ARG="
if not exist "%DATA%\presentation.json" set "LANG_ARG=--language zh-Hans"
"%HERE%Marchwind64.exe" --play --rom "%ROM%" %LANG_ARG% %*
