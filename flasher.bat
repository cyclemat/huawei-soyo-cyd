@echo off
rem Huawei - Soyo: lokaler Web-Flasher (CYD per USB aus dem Browser aufspielen)
cd /d "%~dp0"
where py >nul 2>nul && (py -3 tools\flasher.py) || (python tools\flasher.py)
pause
