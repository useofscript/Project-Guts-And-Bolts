@echo off
rem Starts the Guts&Bolts server on this computer. Keep the window open while people play.
rem Everything it stores (accounts, Bolts, uploads) goes in the server_data folder.
cd /d "%~dp0\.."
python install.py --server
if errorlevel 1 py install.py --server
pause
