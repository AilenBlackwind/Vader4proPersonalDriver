# JoyShockMapper Console Injector

A small helper executable that attaches to the running JoyShockMapper console and
injects any text command you pass it (e.g. a profile path such as `profiles-library/My Profile.txt`) as if the user had typed it manually.

To capture the console output from the injected command, add `--capture` (or `-c`)
as a third argument. Without the flag it behaves exactly as before.

## Building

```
cd JSM_GUI/jsm-gui-app/native/console-injector
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Copy the resulting `build/Release/jsm-console-injector.exe` into
`JSM_GUI/jsm-gui-app/bin/` so the Electron main process can spawn it.
