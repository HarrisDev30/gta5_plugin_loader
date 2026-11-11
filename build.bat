@echo off
setlocal
echo ===================================================
echo   Compiling modHEN GTA V Plugin Loader for PS5
echo ===================================================

set "BASE_DIR=%~dp0.."
set "TOOLDIR=%BASE_DIR%\tools\llvm-mingw-20261104-ucrt-x86_64\bin"
set "SDKDIR=%BASE_DIR%\ps5-payload-sdk"
set "PATH=%TOOLDIR%;%PATH%"

echo Generating web dashboard assets...
python "%~dp0..\tools\generate_web_assets.py"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed generating web_assets.h!
    exit /b 1
)

echo Building ModHEN Orbx Native PIC hook blob...
cd /d "%~dp0src"
"%TOOLDIR%\clang.exe" --target=x86_64-sie-ps5 -Wall -Wextra -O2 -ffreestanding -fPIC -fvisibility=hidden -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -mno-avx -c orbx_native.c -o orbx_native.o
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed compiling orbx_native.c!
    exit /b 1
)
"%TOOLDIR%\clang.exe" --target=x86_64-sie-ps5 -Wall -Wextra -O2 -ffreestanding -fPIC -fvisibility=hidden -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -mno-avx -c orbx_mod_menu.c -o orbx_mod_menu.o
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed compiling orbx_mod_menu.c!
    exit /b 1
)
"%TOOLDIR%\ld.lld.exe" -nostdlib --no-undefined -T orbx_native.ld orbx_native.o orbx_mod_menu.o -o orbx_native_elf.elf
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed linking orbx_native.ld!
    exit /b 1
)
"%TOOLDIR%\llvm-objcopy.exe" -O binary orbx_native_elf.elf orbx_native.bin
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed extracting orbx_native.bin!
    exit /b 1
)
"%TOOLDIR%\llvm-objcopy.exe" -I binary -O elf64-x86-64 -B i386:x86-64 orbx_native.bin orbx_native_bin.o
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed wrapping orbx_native_bin.o!
    exit /b 1
)

echo Embedding Orbx Engine binaries...
cd /d "%~dp0plugins"
"%TOOLDIR%\llvm-objcopy.exe" -I binary -O elf64-x86-64 -B i386:x86-64 orbx.prx "%~dp0src\orbx_prx.o"
"%TOOLDIR%\llvm-objcopy.exe" -I binary -O elf64-x86-64 -B i386:x86-64 orbx.ini "%~dp0src\orbx_ini.o"
cd /d "%~dp0"

echo Compiling ELF payload...
"%TOOLDIR%\clang.exe" --target=x86_64-sie-ps5 ^
    -isystem "%SDKDIR%/target/include" ^
    -L "%SDKDIR%/target/lib" ^
    -fno-stack-protector -fno-plt -femulated-tls ^
    "%~dp0src\main.c" ^
    "%~dp0src\nullfs_mount.c" ^
    "%~dp0src\pad_hook.c" ^
    "%~dp0src\pt.c" ^
    "%~dp0src\hyperhen_injector.c" ^
    "%~dp0src\orbx_compat.c" ^
    "%~dp0src\orbx_native_host.c" ^
    "%~dp0src\modhen_config.c" ^
    "%~dp0src\web_server.c" ^
    "%~dp0src\orbx_native_bin.o" ^
    "%~dp0src\orbx_prx.o" ^
    "%~dp0src\orbx_ini.o" ^
    -lc -lkernel_web -lSceLibcInternal ^
    -o "%~dp0gta5_plugin_loader.elf"

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] Built: %~dp0gta5_plugin_loader.elf
) else (
    echo.
    echo [ERROR] Build failed! Check compiler errors above.
)
