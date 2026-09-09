# Vantage

An open-source embedded OS designed for DIY smartphones and handhelds. Built on ESP-IDF, FreeRTOS, and LVGL, featuring dynamic ELF application loading (.vpk) and real-time window management

Language: C/C++

## Dependencies
* [ESP-IDF](https://dl.espressif.com/dl/eim/index.html) - Main framework
* [LVGL](https://github.com/lvgl/lvgl) - GUI
* [LovyanGFX](https://github.com/lovyan03/LovyanGFX) - Drivers
* [cJSON](https://github.com/Davegamble/cjson) - JSON handler (for saving files I guess..)

## Desktop simulator

The SDL simulator in `sim/` now follows the device UI model. It provides the Home screen, animated app launches,
Back/Home/Recents window management, the on-screen keyboard, Wi-Fi connection
flow, file manager, text editor, create/rename/delete operations, a VPK install
flow, installed-app visibility, and the OTA status flow.

Build it from the repository root:

```powershell
cmake -S sim -B build
cmake --build build --config Debug --target vantage_sim
.\build\Debug\vantage_sim.exe
```

The simulator deliberately keeps side effects separate from a real device:

- Its virtual `/int` and `/sdcard` drives are folders below `sim_data/`.
- On Windows, the Wi-Fi screen reads the host's current connection through the
  WLAN API. It never scans, connects, disconnects, or stores credentials.
  Other desktop targets report that host Wi-Fi status is unavailable.
- The VPK flow exercises installation, registry, Home, and lifecycle UI. An
  ESP ELF is not executable on a desktop; an installed package opens a native
  desktop stand-in until the app is rebuilt for the simulator ABI.
- OTA is represented visually and never writes firmware or changes a boot
  partition.

### Portable apps: run the same app source in both targets

`app_sdk/vantage_app.h` is the portable app boundary. It exposes the app
descriptor lifecycle plus basic window, text, button, toast, and private-data
path APIs without exposing ESP-IDF or SDL headers. The simulator registry is
empty by default, so no sample or placeholder app appears on Home.

To add a simulator-capable app:

1. Write the app source using only `#include "vantage_app.h"`, and export a
   unique descriptor function such as `vantage_notes_descriptor()`.
2. Add the source to the `vantage_sim` executable in `sim/CMakeLists.txt`.
3. Add its descriptor function to `sim/apps/registry.c`.
4. Build the simulator. The app is discovered by Home automatically.

The same source can be compiled into a VPK for the ESP target when its app
build includes `app_sdk/` and `main/` as include directories. The VPK still
contains an ESP ELF, while the simulator compiles the source natively for the
desktop. This is why the UI and portable SDK behavior can be shared even
though the produced binaries are different.
