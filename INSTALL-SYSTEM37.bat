@echo off
title System 37 installer
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-system37.ps1"
pause
