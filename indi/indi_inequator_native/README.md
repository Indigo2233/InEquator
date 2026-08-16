# InEquator native INDI driver

Native C++ INDI driver (class `INDI::Telescope`) for the InEquator ESP8266
single-axis RA tracker.

## Supported controllers

| Controller | Minimum firmware | Connection |
| --- | ---: | --- |
| ESP8266 RA tracker | 2001 | USB serial or TCP |

Serial settings are 9600-8-N-1. TCP uses port 4030 and defaults to
`192.168.4.1`.

## Driver features

- Sidereal tracking engage/disengage (`TELESCOPE_CAN_CONTROL_TRACK`)
- W/E manual jog at 5 preset rates (1x, 8x, 32x, 128x, 256x sidereal)
- Abort motion
- Position readout in steps
- PPM rate correction (maps to firmware `D` command)
- Firmware and controller identification

## Slew rate mapping

| INDI SlewRate index | Label | Firmware `Q` value |
| --- | --- | ---: |
| 0 | 1x | 10000 |
| 1 | 8x | 80000 |
| 2 | 32x | 320000 |
| 3 | 128x | 1280000 |
| 4 | 256x | 2560000 |

## Build

Install libindi development files, CMake, and a C++ compiler, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

Start the driver with:

```bash
indiserver -vv indi_inequator_tracker
```

## License

LGPL-2.1-or-later, matching the license headers in each source file.
