@echo off
REM This file will be placed in the install directory and can be run from there.

"%~dp0bin\rampart.exe" "%~dp0install-helper.js"

REM Refresh PATH in the current session so the user doesn't need to restart
for /f "tokens=2*" %%A in ('reg query "HKCU\Environment" /v Path 2^>nul') do set "USERPATH=%%B"
for /f "tokens=2*" %%A in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v Path 2^>nul') do set "SYSPATH=%%B"
set "PATH=%SYSPATH%;%USERPATH%"
