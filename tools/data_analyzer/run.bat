@echo off
chcp 65001 >nul
py -3 "%~dp0analyzer.py" %*
if errorlevel 1 pause
