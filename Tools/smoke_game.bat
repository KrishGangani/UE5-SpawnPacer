@echo off
rem P02 SpawnPacer - smoke test: start the game, load the default map, run the given console commands, quit.
rem Usage: smoke_game.bat [extra args...]    uncooked: the editor binary in -game mode, no GPU (-nullrhi), no cook needed
rem        smoke_game.bat packaged           the packaged Development build (run package.bat first) on the real GPU,
rem                                          windowed 1280x720, which is the benchmark setting. It starts the REAL
rem                                          game exe (Build\Windows\SpawnPacer\Binaries\Win64), not the bootstrap in
rem                                          Build\Windows, so the exit code is the game's own.
rem Both: no sound, no Insights auto-trace (-traceautostart=0), and the log goes to a fixed file.
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\SpawnPacer\SpawnPacer.uproject"
if /I "%~1"=="packaged" goto packaged

set "LOG=%~dp0..\SpawnPacer\Saved\Logs\SmokeGame.log"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -game -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOG%" %*
exit /b %ERRORLEVEL%

:packaged
set "LOG=%~dp0..\Build\SmokePackaged.log"
"%~dp0..\Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe" -windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOG%"
exit /b %ERRORLEVEL%
