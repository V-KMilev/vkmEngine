@echo off
rem tools/vkm for cmd.exe and PowerShell, which cannot run a file with no extension.
where py >nul 2>nul
if %errorlevel% == 0 (
    py -3 "%~dp0vkm" %*
) else (
    python "%~dp0vkm" %*
)
exit /b %errorlevel%
