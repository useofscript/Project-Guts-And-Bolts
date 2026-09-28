@echo off
rem Double-click me to install Guts and Bolts on Windows.
title Guts and Bolts installer
cd /d "%~dp0"

rem Use Python if it's already here.
where py >nul 2>nul && ( py -3 install.py %* & goto :end )
for %%P in (python.exe) do if not "%%~$PATH:P"=="" (
    python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>nul && ( python install.py %* & goto :end )
)

echo Python is needed to run the installer. Installing it now (this only happens once)...
where winget >nul 2>nul || (
    echo.
    echo Please install Python 3 from https://www.python.org/downloads/
    echo Tick "Add python.exe to PATH" during setup, then double-click Install.bat again.
    start https://www.python.org/downloads/
    pause
    goto :end
)
winget install -e --id Python.Python.3.12 --accept-source-agreements --accept-package-agreements
if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" (
    "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" install.py %*
) else (
    echo.
    echo Python was installed. Please double-click Install.bat again.
    pause
)
:end
