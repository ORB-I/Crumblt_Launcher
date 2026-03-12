@echo off
echo Registering crumblt:// protocol handler...

REG ADD "HKCU\SOFTWARE\Classes\crumblt" /ve /d "Crumblt Game Launcher" /f
REG ADD "HKCU\SOFTWARE\Classes\crumblt" /v "URL Protocol" /d "" /f
REG ADD "HKCU\SOFTWARE\Classes\crumblt\DefaultIcon" /ve /d "%~dp0CrumbltLauncher.exe,0" /f
REG ADD "HKCU\SOFTWARE\Classes\crumblt\shell\open\command" /ve /d "\"%~dp0CrumbltLauncher.exe\" \"%%1\"" /f

echo Done! crumblt:// links will now launch CrumbltLauncher.exe
pause
