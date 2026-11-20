@echo off
rem P02 SpawnPacer - package a standalone Win64 build (BuildCookRun: build, cook, stage, pak, archive).
rem Usage: package.bat [Configuration]   Configuration: Development (default; headline benchmarks) | Shipping (no CSV profiler; not used)
rem The launcher (installed) engine cannot build the Test configuration, so headline benchmarks use packaged Development.
rem Output: ..\Build\Windows\SpawnPacer.exe   (Build\ is git-ignored)
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\SpawnPacer\SpawnPacer.uproject"
set "ARCHIVE=%~dp0..\Build"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -noP4 -utf8output -unattended -platform=Win64 -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory="%ARCHIVE%"
exit /b %ERRORLEVEL%
