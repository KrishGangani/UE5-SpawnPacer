@echo off
rem P02 SpawnPacer - build one target from the command line (reproducible; no IDE needed).
rem Usage: build.bat [Target] [Configuration]
rem   Target:        SpawnPacerEditor (default) | SpawnPacer
rem   Configuration: Development (default) | Shipping | DebugGame   (Test cannot be built by the launcher engine)
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\SpawnPacer\SpawnPacer.uproject"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=SpawnPacerEditor"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project="%PROJECT%" -WaitMutex -NoHotReloadFromIDE
exit /b %ERRORLEVEL%
