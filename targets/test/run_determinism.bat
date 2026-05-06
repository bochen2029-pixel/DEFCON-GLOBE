@echo off
rem Phase 0 determinism runner (Windows side).
rem See run_determinism.sh for documentation.

setlocal

set scenario=%1
set ticks=%2
set out=%3

if "%scenario%"=="" set scenario=surface
if "%ticks%"==""    set ticks=10000
if "%out%"==""      set out=%TEMP%\defcon-determinism.trace

if not "%scenario%"=="surface" if not "%scenario%"=="mirv" if not "%scenario%"=="radar" (
    echo unknown scenario: %scenario% 1>&2
    exit /b 2
)

if "%DEFCON_BIN%"=="" set DEFCON_BIN=Defcon.exe
if not exist "%DEFCON_BIN%" (
    echo DEFCON binary not found at %DEFCON_BIN% 1>&2
    exit /b 3
)

set DEFCON_DETERMINISM_TRACE=%out%
set DEFCON_DETERMINISM_SCENARIO=%scenario%
set DEFCON_DETERMINISM_TICKS=%ticks%

"%DEFCON_BIN%"

if not exist "%out%" (
    echo no trace produced at %out% 1>&2
    exit /b 4
)

echo wrote trace to %out%
