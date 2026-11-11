@echo off
setlocal
title Upload GTA V Plugins to PS5

set "PYTHON_EXE=python"
where python >nul 2>&1
if %ERRORLEVEL% neq 0 (
    if exist "%~dp0..\tools\llvm-mingw-20261104-ucrt-x86_64\python\bin\python.exe" (
        set "PYTHON_EXE=%~dp0..\tools\llvm-mingw-20261104-ucrt-x86_64\python\bin\python.exe"
    )
)

"%PYTHON_EXE%" "%~dp0upload_plugins.py" %*
echo.
pause

