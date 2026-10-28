@echo off
setlocal
pushd "%~dp0"
main-calibration-v2.exe --gyro-log
popd
