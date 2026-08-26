@echo off
call "%~dp0build.cmd" -Package %*
exit /b %errorlevel%
