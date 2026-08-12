@echo off
REM ============================================================================
REM  Wizard.bat - Lanza el Route Wizard de BZ_AutoDrive (doble clic).
REM  -ExecutionPolicy Bypass: corre el .ps1 sin tocar la politica del sistema
REM  (solo afecta a ESTA ejecucion; no es un cambio permanente ni inseguro).
REM ============================================================================
chcp 65001 >nul
title BZ_AutoDrive - Route Wizard
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0route_wizard.ps1" %*
if errorlevel 1 pause
