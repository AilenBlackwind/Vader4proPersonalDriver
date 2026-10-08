@echo off
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" exit /b 1
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "TESTDIR=%TEMP%\VaderDriverTests-%RANDOM%-%RANDOM%"
mkdir "%TESTDIR%"
if errorlevel 1 exit /b 1
pushd "%TESTDIR%"
set "SDL3DIR=%REPO%\SDL3-devel\SDL3-3.4.12"
cl /nologo /EHsc /std:c++17 /DNOMINMAX "%REPO%\tests\gyro_recovery_tests.cpp" "%REPO%\src\gyro\joystick_mode.cpp" /link user32.lib /OUT:gyro-recovery-tests.exe
if errorlevel 1 goto failed
gyro-recovery-tests.exe
if errorlevel 1 goto failed
cl /nologo /EHsc /std:c++17 /DNOMINMAX "%REPO%\tests\gyro_acceleration_tests.cpp" "%REPO%\src\gyro\joystick_mode.cpp" /link user32.lib /OUT:gyro-acceleration-tests.exe
if errorlevel 1 goto failed
gyro-acceleration-tests.exe
if errorlevel 1 goto failed
cl /nologo /EHsc /std:c++17 /DNOMINMAX "%REPO%\tests\config_tests.cpp" "%REPO%\src\config\config.cpp" /link user32.lib /OUT:config-tests.exe
if errorlevel 1 goto failed
config-tests.exe
if errorlevel 1 goto failed
cl /nologo /EHsc /std:c++17 /DNOMINMAX /I "%SDL3DIR%\include" "%REPO%\tests\hid_path_tests.cpp" "%REPO%\src\haptics\haptic_pipe.cpp" "%REPO%\src\config\config.cpp" "%REPO%\src\gyro\gyro_processing.cpp" "%REPO%\src\gyro\joystick_mode.cpp" "%REPO%\src\gyro\profile_logic.cpp" /link "%SDL3DIR%\lib\x64\SDL3.lib" user32.lib /OUT:hid-path-tests.exe
if errorlevel 1 goto failed
copy /y "%SDL3DIR%\lib\x64\SDL3.dll" . >nul
if errorlevel 1 goto failed
hid-path-tests.exe
if errorlevel 1 goto failed
cl /nologo /EHsc /std:c++17 /DNOMINMAX /I "%SDL3DIR%\include" "%REPO%\tests\app_runtime_tests.cpp" "%REPO%\src\haptics\haptic_pipe.cpp" "%REPO%\src\config\config.cpp" "%REPO%\src\gyro\gyro_processing.cpp" "%REPO%\src\gyro\joystick_mode.cpp" "%REPO%\src\gyro\profile_logic.cpp" /link "%SDL3DIR%\lib\x64\SDL3.lib" user32.lib /OUT:app-runtime-tests.exe
if errorlevel 1 goto failed
app-runtime-tests.exe
if errorlevel 1 goto failed
cl /nologo /EHsc /std:c++17 /DNOMINMAX /I "%SDL3DIR%\include" "%REPO%\tests\haptic_pipe_tests.cpp" /link "%SDL3DIR%\lib\x64\SDL3.lib" /OUT:haptic-pipe-tests.exe
if errorlevel 1 goto failed
haptic-pipe-tests.exe
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
