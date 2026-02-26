@echo off
setlocal enabledelayedexpansion

REM This file will be placed in the install directory and can be run from there.

cd /d "%~dp0"

REM Check for dev\shm directory (required by Cygwin runtime)
if not exist "%~dp0..\dev\shm" (
    echo The directory "%~dp0..\dev\shm" is required by the Cygwin runtime but does not exist.
    set /p RESP="Create it? (Y/n) "
    if "!RESP!"=="" set RESP=Y
    if /i "!RESP!"=="Y" (
        mkdir "%~dp0..\dev\shm"
        echo Created "%~dp0..\dev\shm"
    ) else (
        echo Cannot run tests without dev\shm. Exiting.
        exit /b 1
    )
)

set FAILED=0
set PASSED=0

for %%f in (test\*-test.js) do (
    echo.
    echo %%~nxf
    bin\rampart.exe "%%f"
    if !errorlevel! neq 0 (
        echo Test %%~nxf failed
        set /a FAILED+=1
    ) else (
        set /a PASSED+=1
    )
)

REM also run the rampart-url.js, which has its own tests
echo.
echo modules\rampart-url.js
bin\rampart.exe modules\rampart-url.js
if !errorlevel! neq 0 (
    echo Test rampart-url.js failed
    set /a FAILED+=1
) else (
    set /a PASSED+=1
)

echo.
if !FAILED! gtr 0 (
    echo %PASSED% passed, %FAILED% failed.
    exit /b 1
) else (
    echo All %PASSED% tests passed.
)
