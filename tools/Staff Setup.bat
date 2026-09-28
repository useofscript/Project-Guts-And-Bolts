@echo off
rem Project owner only: makes this computer's account the official "Guts" staff account.
cd /d "%~dp0\.."
call Install.bat --staff
pause
