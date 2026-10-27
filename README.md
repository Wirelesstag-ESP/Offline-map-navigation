# WT99P4C5-S1 Simple Map

This standalone ESP-IDF project ports the upstream
[`0015/map_tiles_projects`](https://github.com/0015/map_tiles_projects)
`01.Simple_Map` interface to the WT99P4C5-S1. It supports the board's 1024 x 600
EK79007 display, GT911 touch controller, SDMMC slot, an LC76G GNSS receiver,
and an optional GY-BNO085. Map rendering is offline; the ESP32-C5 radio is not
used.

- [中文使用指南](docs/使用指南.md)
- [开发日志与问题复盘](docs/开发日志.md)

The project retains the upstream MIT license and the original copyright notice.
Board-specific components keep their own license files.

## Project layout

```text
.
├── main/                       # Application startup and UI integration
├── components/
│   ├── simple_map/             # Board-independent map and tile logic
│   ├── wt99p4c5_s1_board/      # Display, touch, SD-card, and power BSP
│   ├── gps_lc76g/              # GNSS receiver support
│   └── bno085_attitude/        # Optional heading sensor support
├── docs/                       # Chinese guide and development log
├── tools/                      # Tile validation utilities
├── tests/                      # Host-side validation tests
├── CMakeLists.txt
├── partitions.csv
└── sdkconfig.defaults
```

The repository root is the ESP-IDF project directory. Generated build files,
managed dependencies, local `sdkconfig`, and map data are intentionally not
tracked by Git.

## LC76G wiring

| WT99P4C5-S1 | LC76G |
| --- | --- |
| GPIO4 (UART1 TX) | RX |
| GPIO5 (UART1 RX) | TX |
| 3V3 | VCC |
| GND | GND |

Disconnect power before wiring and cross the UART signals: host TX connects to
module RX, and module TX connects to host RX. The Waveshare LC76G GNSS Module
carrier accepts 3.3 V to 5 V input; this project uses the board's 3.3 V supply.
Leave PPS, RST, SDA, and SCL disconnected. The UART configuration is 115200
baud, 8 data bits, no parity, and one stop bit.

At startup the application waits for the first valid, recent position fix. A
latitude/longitude form remains available so the map can be opened indoors or
without the receiver. After a fix, the blue marker and satellite count update
continuously. Dragging the map pauses automatic following; press the round GPS
button to resume following and center the map on the latest fix.

Connect the GNSS antenna before testing and place it outdoors with a clear view
of the sky. A cold start can take several minutes in poor conditions. The power
LED only confirms power; wait for NMEA data and a valid fix in the serial log to
confirm reception.

## GY-BNO085 wiring

Disconnect power before wiring. This target uses the BNO085's UART-RVC mode so
that it does not interfere with the display direction-control pins. In
particular, completely disconnect any old BNO085 wires from GPIO26 and GPIO27:
those pins drive the LCD's `UPDN_CTRL` and `SHLR_CTRL` signals on this board.

| GY-BNO085 | WT99P4C5-S1 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA/TX | GPIO33 (UART2 RX) |
| RST | 3V3 (held inactive) |
| PS0/P0 | 3V3 |
| PS1/P1 | GND |

Leave SCL, ADO/DI, CS, and INT disconnected. GPIO33 is also routed to the
Ethernet PHY's RMII TXEN input, so BNO085 UART-RVC and Ethernet cannot be used
at the same time; this offline target does not initialize Ethernet.

UART-RVC runs at 115200 baud, 8 data bits, no parity, and one stop bit. The map
remains north-up while the white needle on the blue GPS marker follows heading.
RVC supplies heading, pitch, and roll but not the SH-2 `C0` through `C3`
accuracy status. Press the round refresh button to set the mounting offset for
the current power session; it is intentionally not stored across sensor resets.
If the IMU is missing or stops reporting, the application keeps the map and
GNSS running and falls back to a directionless GPS dot.

## Prepare map tiles

The input must use the standard Slippy Map layout:

```text
png_tiles/
  16/
    53420/
      26155.png
```

Each PNG must be exactly 256 x 256 pixels. This is the standard size of one
tile, not the size of the complete map. Adjacent `x` and `y` files form the map,
and each zoom level contains a different tile grid.

First configure the firmware once so ESP-IDF downloads `map_tiles 1.3.0` and
its upstream converter:

```sh
. /home/dinosaur/esp/esp-idf/export.sh
git clone https://github.com/Susan-fish/map_tiles_projects.git
cd map_tiles_projects
idf.py reconfigure
```

Install the desktop-only image dependency in a virtual environment, then run
the converter supplied by `map_tiles`:

```sh
python3 -m venv /tmp/simple-map-tile-env
/tmp/simple-map-tile-env/bin/pip install -r tools/requirements-map-tiles.txt
/tmp/simple-map-tile-env/bin/python \
  managed_components/0015__map_tiles/script/lvgl_map_tile_converter.py \
  --input /path/to/png_tiles \
  --output /path/to/sdcard/tiles1 \
  --jobs 4
python3 tools/validate_map_tiles.py /path/to/sdcard/tiles1
```

The converter writes a 12-byte LVGL 9 header followed by little-endian RGB565
pixels. Copy the resulting `tiles1` directory to the root of a FAT-formatted SD
card. The firmware reads `/sdcard/tiles1/<zoom>/<x>/<y>.bin`.

The example zoom-11 data used during development covered approximately
80.16°E–90.18°E and 22.27°N–51.84°N. To reproduce that test with the same
5×5 tile area, use
latitude `30.675715` and longitude `85.166016`. Coordinates outside the
converted coverage may show missing tiles. The example tiles are generated
data and are not distributed in this repository.

With SD-card storage the UI permits zoom levels 2 through 11 and starts at
zoom 10. The internal Flash fallback contains only the small development set,
so it permits zoom levels 9 through 10 and also starts at zoom 10.

Only convert tiles you are permitted to store and use. Do not bulk-download
from a public interactive tile server unless its usage policy explicitly
allows that workload.

## Build and flash

```sh
. /home/dinosaur/esp/esp-idf/export.sh
cd map_tiles_projects
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Replace the port with the device shown on the host. Building does not require a
connected board.

Run the host-side UART-RVC parser tests separately:

```sh
cmake -S components/bno085_attitude/test -B build_host/bno085_parser_test
cmake --build build_host/bno085_parser_test
ctest --test-dir build_host/bno085_parser_test --output-on-failure
```

### Internal Flash test tiles

For development without an SD-card reader, put an already converted `tiles1`
tree in `storage_data/tiles1` before configuring the project. The build creates
a SPIFFS image for the `storage` partition and `idf.py flash` writes it together
with the application. At runtime the application prefers an SD card containing
a `tiles1` directory and otherwise falls back to the internal partition at the
same `/sdcard` mount point.

The internal partition is intended for a small test area only. It is not large
enough for the nationwide tile set. `storage_data` is ignored by Git because it
contains generated map data.

This target intentionally supports ESP32-P4 revisions below v3, including the
WT99P4C5-S1 v1.3 hardware. If esptool reports that an image requires v3.1 or
newer, regenerate the build from this target's `sdkconfig.defaults`; do not
override the revision check with `--force`.
