@echo off
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" exit /b 1
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "TESTDIR=%TEMP%\VaderReplay-%RANDOM%-%RANDOM%"
mkdir "%TESTDIR%"
if errorlevel 1 exit /b 1
pushd "%TESTDIR%"
cl /nologo /EHsc /std:c++17 /DNOMINMAX "%REPO%\tests\gyro_calibration_tests.cpp" "%REPO%\src\gyro\joystick_mode.cpp" "%REPO%\src\config\config.cpp" /link user32.lib /OUT:gyro-calibration-tests.exe
if errorlevel 1 goto failed
popd
pushd "%REPO%"
"%TESTDIR%\gyro-calibration-tests.exe" --report default.json gyro-replay.html
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
