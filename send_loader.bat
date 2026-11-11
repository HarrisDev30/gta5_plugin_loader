@echo off
setlocal
echo ===================================================
echo   Send GTA V Plugin Loader Payload to PS5
echo ===================================================

set "IP_FILE=%~dp0..\ps5_ip.txt"
set "ELF_FILE=%~dp0gta5_plugin_loader.elf"

if not exist "%ELF_FILE%" (
    echo [ERROR] %ELF_FILE% not found! Run build.bat first.
    pause
    exit /b 1
)

if exist "%IP_FILE%" (
    set /p PS5_IP=<"%IP_FILE%"
)

if "%PS5_IP%"=="" (
    set /p PS5_IP="Enter your PS5 IP Address (e.g. 10.21.19.177): "
    echo %PS5_IP%>"%IP_FILE%"
) else (
    echo Using saved PS5 IP: %PS5_IP%
    set /p CHANGE_IP="Press ENTER to use this IP, or type a new IP: "
    if not "%CHANGE_IP%"=="" (
        set "PS5_IP=%CHANGE_IP%"
        echo %CHANGE_IP%>"%IP_FILE%"
    )
)

echo.
echo Sending Plugin Loader payload to %PS5_IP% (port 9020 / 9021)...

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ip = '%PS5_IP%';" ^
    "$file = '%ELF_FILE%';" ^
    "$bytes = [System.IO.File]::ReadAllBytes($file);" ^
    "$ports = @(9020, 9021);" ^
    "$success = $false;" ^
    "foreach ($port in $ports) {" ^
    "    try {" ^
    "        Write-Host ('Attempting connection to ' + $ip + ':' + $port + '...') -ForegroundColor Yellow;" ^
    "        $client = New-Object System.Net.Sockets.TcpClient;" ^
    "        $connect = $client.BeginConnect($ip, $port, $null, $null);" ^
    "        $wait = $connect.AsyncWaitHandle.WaitOne(2000, $false);" ^
    "        if (-not $wait) { $client.Close(); continue; }" ^
    "        $client.EndConnect($connect);" ^
    "        $stream = $client.GetStream();" ^
    "        $stream.Write($bytes, 0, $bytes.Length);" ^
    "        $stream.Flush();" ^
    "        $stream.Close();" ^
    "        $client.Close();" ^
    "        Write-Host ('[SUCCESS] Payload sent to ' + $ip + ':' + $port + '!') -ForegroundColor Green;" ^
    "        $success = $true;" ^
    "        break;" ^
    "    } catch {" ^
    "        Write-Host ('Port ' + $port + ' failed: ' + $_.Exception.Message) -ForegroundColor DarkGray;" ^
    "    }" ^
    "}" ^
    "if (-not $success) {" ^
    "    Write-Host '[ERROR] Failed to send payload to PS5. Make sure ELF loader is listening.' -ForegroundColor Red;" ^
    "    exit 1;" ^
    "}"

echo.
pause

