@echo off
cd /d "%~dp0"
".\modengine2_launcher.exe" -t er -c ".\config_sharedloot_steam.toml"
if errorlevel 1 pause
