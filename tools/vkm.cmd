@echo off
rem vkm for cmd.exe and PowerShell: runs vkm.py on the SDK's own Python, or on the
rem system's in the engine's tree.
setlocal
set "VKM_PROG=%~dp0vkm.py"
if exist "%~dp0bin\vkm.py" set "VKM_PROG=%~dp0bin\vkm.py"
if exist "%~dp0python\python.exe" (
    "%~dp0python\python.exe" "%VKM_PROG%" %*
) else (
    where py >nul 2>nul
    if errorlevel 1 (
        python "%VKM_PROG%" %*
    ) else (
        py -3 "%VKM_PROG%" %*
    )
)
exit /b %errorlevel%
