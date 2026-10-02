@echo off
setlocal
rem Set CMAKE_PREFIX_PATH to your Qt 6 directory before running this script,
rem and run it from a prompt where the matching compiler is on PATH.
cmake -S "%~dp0" -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build "%~dp0build"
if errorlevel 1 exit /b 1
echo Build completed. Use windeployqt on build\dino-stalker-unpacker.exe to bundle the Qt runtime.
