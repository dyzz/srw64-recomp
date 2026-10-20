@echo off
rem All directory, ROM and language policy is shared with the native Windows launcher.
rem Create a user-data folder beside Marchwind64.exe for portable settings and saves.
setlocal
"%~dp0Marchwind64.exe" --play %*
exit /b %errorlevel%
