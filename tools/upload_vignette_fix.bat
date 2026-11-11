@echo off
setlocal
echo ===================================================
echo   Upload Soft Vignette Fix to PS5
echo ===================================================

set "IP_FILE=%~dp0..\ps5_ip.txt"
if exist "%IP_FILE%" (
    set /p PS5_IP=<"%IP_FILE%"
)
if "%PS5_IP%"=="" set "PS5_IP=10.138.159.177"

echo Using PS5 IP: %PS5_IP%
python "%~dp0upload_vignette_fix.py" %PS5_IP%
pause

