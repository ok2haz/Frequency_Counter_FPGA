@echo off
REM Wrapper pro CubeMX "Script (after generation)".
REM CubeMX neumi spustit .ps1 primo (a nezna ExecutionPolicy), proto tenhle .bat.
REM Nastav v Project Manager -> Project -> Script (after generation):
REM     <cesta k projektu>\tools\post_generate.bat
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0post_generate.ps1"
exit /b %ERRORLEVEL%
