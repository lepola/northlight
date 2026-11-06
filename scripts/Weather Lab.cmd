@echo off
python "%~dp0weather_lab.py" %*
if errorlevel 1 pause
