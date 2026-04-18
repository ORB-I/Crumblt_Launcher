@echo off
echo Building Crumblt Launcher Release...

cargo build --release
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    pause
    exit /b 1
)

copy target\release\crumblt-launcher.exe CrumbltLauncher.exe
echo.
echo Release built: CrumbltLauncher.exe
echo Ready for GitHub release!
pause
