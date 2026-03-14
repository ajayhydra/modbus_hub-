@echo off
REM ModbusHub - Windows Build Script Wrapper
REM Usage: build_windows.bat [clean|build|run|all|rebuild|package]
REM
REM  package  - Build and create dist\ModbusHub_portable.zip
REM             (copy to any Windows PC - no Qt install required)

setlocal
set ACTION=%1
if "%ACTION%"=="" set ACTION=build

REM Always use PowerShell script with Bypass policy
powershell -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1" %ACTION%
endlocal
