@echo off
rem Thin wrapper: all build logic (VS location via vswhere, cmake+ninja flow,
rem running-exe pre-flight, per-target freshness verification, honest banner)
rem lives in tools\build.py.  Usage: tools\build.bat [ct|Belthasar|ct_world|ct_viewer]
rem (no argument = build everything, exactly as before).
setlocal
set "PY=python"
where python >nul 2>nul
if errorlevel 1 set "PY=py"
"%PY%" "%~dp0build.py" %*
exit /b %ERRORLEVEL%
