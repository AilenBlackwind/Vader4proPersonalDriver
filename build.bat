@echo off
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
    echo Could not find vcvars64.bat
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 exit /b %errorlevel%

cl.exe /nologo /EHsc /std:c++17 Main.cpp src\\runtime\\app.cpp src\\haptics\\haptic_pipe.cpp src\\config\\config.cpp src\\gyro\\gyro_processing.cpp src\\gyro\\joystick_mode.cpp src\\gyro\\profile_logic.cpp /link hidapi.lib winmm.lib Psapi.lib user32.lib /OUT:main.exe
exit /b %errorlevel%
