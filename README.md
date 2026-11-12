# Time Keys

Set the time of day in Motor Town with one key.

| | |
|---|---|
| Page Up | 12:00, noon |
| Page Down | 00:00, midnight |

A beep confirms it. The hours are settings: `plugins	ime_keys.ini`, `[TimeKeys] DayHour=12`, `NightHour=0`;
`[General] Disabled=1` turns the plugin off.

Uses the game's own time-of-day command, so it works in single player and for the host. On someone else's server the
server keeps its clock.

## Install

Needs [Plugin Loader](https://github.com/qxZap/mt-plugin-loader) (`version.dll`) in `MotorTown\Binaries\Win64\`.
Put `mt_plugin_TimeKeys.dll` in `MotorTown\Binaries\Win64\plugins\`.

## Build

`build.bat` (MSVC 2022, x64) writes `out\mt_plugin_TimeKeys.dll`.
