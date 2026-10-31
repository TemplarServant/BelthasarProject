@echo off
rem Thin wrapper: the build logic (VS location via vswhere, cmake+ninja,
rem running-exe pre-flight, per-target artifact check) lives in tools\build.py.
rem Usage: tools\build.bat [ct|Belthasar|ct_world|ct_viewer]  (no argument = build everything)
setlocal
set "PY=python"
where python >nul 2>nul
if errorlevel 1 set "PY=py"
"%PY%" "%~dp0build.py" %*
exit /b %ERRORLEVEL%
