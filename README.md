# CRT

A Windows desktop CRT television simulator built with C++20, Win32, Direct3D 11, and HLSL. It displays animated static, color bars, a channel indicator, and a power-off effect.

## Build

Open `CRT.slnx` in Visual Studio with the Desktop development with C++ workload, MSVC v145, and a Windows 10/11 SDK installed. Build Debug or Release for x64 or Win32. Shader Model 5.0 shaders are compiled and embedded as resources during the build.

From a Visual Studio developer terminal:

```powershell
msbuild CRT/CRT.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64
```

The project selects the 64-bit compiler tools. Direct3D feature level 11.0 hardware is required to run the app.

## Controls

- Up / Down: switch between four channels.
- Space: power off; press again after the fade completes to power on.
- Escape: exit.
- M: mute/unmute all sound (the title shows the muted state).

Audio is generated with XAudio2: channels 1–3 have progressively quieter static, channel 4 plays a steady 1 kHz test tone, and channel/power changes play short effects. Both static and the test tone fade with screen power and obey the M mute control. If no audio device is available, the simulator continues silently and reports this in the title.

The window supports resizing and minimize/restore.

## Validation

Debug and Release builds for x64 and Win32 were rebuilt successfully. Manual checks covered static and color-bar rendering, channel changes, resizing, power off/on, and minimize/restore.
