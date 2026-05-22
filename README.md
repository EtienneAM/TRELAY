# TRELAY - Matter-over-Thread Relay Controller

A Matter-compatible relay controller for ESP32-C6 that works over Thread networking. Control any relay, solenoid, or dry-contact load from Home Assistant, Apple Home, Google Home, or any Matter-compatible smart home platform — no WiFi, no cloud required.

Originally designed as a **door buzzer controller**: commission it as a Door Lock, press Unlock in Home Assistant, the relay energizes for a few seconds, then automatically re-locks.

> **Disclaimer:** This entire firmware was written by AI ([Claude](https://claude.ai) by Anthropic). I ([@maui1911](https://github.com/maui1911)) have not read or written a single line of code — I only provided direction, tested on real hardware, and deployed. Use at your own risk.

## Features

- **Matter over Thread** - Native Matter protocol, no cloud or WiFi required
- **Two device types** - Generic On/Off switch or Door Lock, selectable at runtime
- **Auto-revert** - Relay automatically turns off after N seconds (required for Door Lock, optional for On/Off)
- **Thread mesh networking** - Self-healing network, device acts as a full Thread router
- **Web-based installer** - Flash and configure directly from your browser
- **USB configuration** - Change all settings via serial without recompiling
- **NVS persistence** - Settings and relay state survive reboots
- **Temperature monitoring** - Chip temperature exposed to Home Assistant as a sensor entity
- **Health monitoring** - Watchdog timer, heap tracking, auto-reboot on hang
- **Power-on behavior** - Configurable: restore last state, always on, or always off

## Hardware

Developed for the **[DFRobot Beetle ESP32-C6](https://wiki.dfrobot.com/SKU_DFR1117_Beetle_ESP32_C6)** — a tiny 25 × 20.5 mm board. Any ESP32-C6 board works.

### Requirements

- **ESP32-C6 board** - DFRobot Beetle ESP32-C6 recommended (or any ESP32-C6)
- **Relay module** - Any 3.3 V or 5 V trigger relay (active-high or active-low)
- **Power supply** - 5 V for the board; relay coil power as required by your module
- **Thread border router** - HomePod Mini, Apple TV 4K, Google Nest Hub, or a dedicated border router (SLZB-06, SMLight, etc.)

### Wiring

```
ESP32-C6          Relay Module
─────────         ────────────
GPIO 5  ────────  IN  (signal)
3.3 V   ────────  VCC (if 3.3 V module)
GND     ────────  GND

Relay NO/COM contacts ──── your door buzzer / load
```

GPIO pin is configurable. Default is GPIO 5. Avoid GPIO 9 (boot button), 12–13 (USB), and 15 (onboard LED on the Beetle).

### Active-high vs active-low

Most relay modules trigger when the signal pin goes **HIGH** (active-high). Some opto-isolated modules trigger when the signal goes **LOW** (active-low). Set `TRELAY_ACTIVE_HIGH` in `menuconfig` to match your module (default: active-high).

### 3D Printable Enclosure

A parametric OpenSCAD enclosure sized for the DFRobot Beetle ESP32-C6 is included in `enclosure/`. Ready-to-print STL files are included. Open `tled_enclosure.scad` in [OpenSCAD](https://openscad.org/) to adjust dimensions.

**Printing tips:** print the base upside-down, 0.2 mm layer height, no supports needed, PLA or PETG recommended.

## Quick Start

### 1. Flash the Firmware

Visit the **[Web Installer](https://maui1911.github.io/TLED/relay)** and click "Install TRELAY Firmware".

> Requires Chrome or Edge. If prompted, hold the BOOT button while clicking Install.

### 2. Configure the Device

1. Go to the **Configure** tab in the web installer
2. Click **Connect to Device**
3. Set your GPIO pin, device type, and auto-revert time
4. Click **Save & Reboot**

### 3. Commission to Your Smart Home

After reboot a QR code appears in the web installer. Scan it with:
- **Home Assistant** → Settings → Devices & Services → Add Integration → Matter
- **Apple Home** → Add Accessory → Scan QR Code
- **Google Home** → Add Device → Matter-enabled device

> **After changing device type** (On/Off ↔ Door Lock), remove and re-add the device in your smart home app — the Matter endpoint type changes.

## Configuration Options

| Setting | Default | Description |
|---------|---------|-------------|
| GPIO Pin | 5 | GPIO pin connected to the relay IN signal |
| Device Type | on_off | `on_off` = generic switch/outlet · `door_lock` = lock |
| Auto-revert | 0 | Seconds before relay turns off automatically. Door Lock: 1–10 s (required). On/Off: 0–3600 s (0 = disabled). |
| Device Name | TRELAY | Name shown in your smart home app |
| Power-on | restore | Behavior on power-up: `restore` / `on` / `off`. Door Lock always starts locked. |

## Device Types

### On/Off Switch (default)
Shows as a generic outlet/switch in Home Assistant. Freely toggle on and off. Auto-revert is optional — useful if you want a timed pulse (e.g. trigger a gate for 2 seconds).

### Door Lock
Shows as a lock in Home Assistant with Lock/Unlock controls. **Auto-revert is mandatory (1–10 s)** — the relay energizes when you unlock, then automatically re-locks after the configured delay. This prevents the relay from staying energized indefinitely.

> **Door buzzer use case:** Set type `door_lock`, revert `5` seconds. Tap Unlock in HA → buzzer energizes → door opens → relay de-energizes after 5 s.

## Serial Commands

Connect via USB and use the serial console in the web installer, or any terminal at 115200 baud:

```
help                       Show available commands
config                     Show current configuration
set gpio <n>               Set relay GPIO pin (0-23, avoid 9/12/13/15)
set type <type>            Device type: on_off | door_lock
set revert <n>             Auto-revert seconds (door_lock: 1-10, on_off: 0-3600)
set name <name>            Set device name (shown in HA)
set poweron <mode>         Power-on behavior: restore | on | off
save                       Save configuration and reboot
reboot                     Restart device without saving
factory                    Factory reset — erases all settings and commissioning data
```

## Building from Source

### Prerequisites

- [ESP-IDF v5.4+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/)
- [ESP-Matter](https://github.com/espressif/esp-matter)

### Build & Flash

```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh

idf.py build

# Flash (keeps commissioning data)
idf.py -p /dev/ttyACM0 flash

# Full erase then flash (clears commissioning — re-pair required)
idf.py -p /dev/ttyACM0 erase-flash flash
```

### Build-time Defaults (menuconfig)

```bash
idf.py menuconfig
# Navigate to "TRELAY Configuration"
```

| Option | Default | Description |
|--------|---------|-------------|
| `TRELAY_GPIO_PIN` | 5 | Default relay GPIO pin |
| `TRELAY_ACTIVE_HIGH` | y | Relay triggers on HIGH (set n for active-low) |
| `TRELAY_DEVICE_TYPE` | 0 | 0 = On/Off, 1 = Door Lock |
| `TRELAY_AUTO_REVERT_S` | 0 | Default auto-revert delay in seconds |

## Troubleshooting

### Can't flash the device
- Use Chrome or Edge (Firefox doesn't support Web Serial API)
- Hold the BOOT button while clicking Install
- Try a different USB cable (some are charge-only)

### Can't find device during commissioning
- Commission within 30 seconds of boot (BLE advertising slows after that)
- Run `factory` command if the device was previously commissioned
- Move closer to your Thread border router

### Relay doesn't respond
- Verify the GPIO pin matches your wiring (`config` command shows current setting)
- Check active-high vs active-low setting for your relay module
- Test the relay manually: `set poweron on` → `save` (boots with relay on)

### Device shows as wrong type in Home Assistant
- Run `set type door_lock` (or `on_off`), then `save`
- Remove and re-add the device in Home Assistant after changing type

### Door lock won't stay unlocked
- This is by design — auto-revert is mandatory for the Door Lock device type (1–10 s max)

## Project Structure

```
TRELAY/
├── main/
│   ├── app_main.cpp            # Matter setup, conditional endpoint creation
│   ├── app_driver.cpp          # Relay GPIO driver, auto-revert timer
│   ├── app_nvs_config.cpp      # Runtime configuration storage
│   ├── app_serial_config.cpp   # USB serial command interface
│   ├── app_monitoring.cpp      # Health monitoring, watchdog, temperature
│   ├── app_device_info.cpp     # Matter device branding
│   ├── app_ble_config.cpp      # BLE configuration service
│   └── Kconfig.projbuild       # Build-time configuration options
├── web-installer/
│   ├── index.html              # Web installer & configurator
│   └── manifest.json           # ESP Web Tools manifest
├── enclosure/
│   └── tled_enclosure.scad     # Parametric OpenSCAD enclosure
├── partitions.csv              # Flash partition layout
└── sdkconfig.defaults          # Default SDK configuration
```


## Features

- **Matter over Thread** - Native Matter protocol, no cloud or WiFi required
- **Full RGB + RGBW control** - Color picker, brightness, on/off from your smart home app
- **Smooth transitions** - 300ms fades on all changes
- **Thread mesh networking** - Self-healing network, device acts as a router
- **Web-based installer** - Flash firmware directly from your browser
- **USB configuration** - Change settings via serial without recompiling
- **NVS persistence** - Settings survive reboots
- **Temperature monitoring** - Chip temperature exposed to Home Assistant
- **Health monitoring** - Watchdog timer, heap tracking, auto-reboot on hang
- **Power-on behavior** - Configurable: restore last state, always on, or always off
- **Built-in effects** - Rainbow, breathing, candle, chase (implemented but not yet user-accessible, untested)

## Hardware

This project was developed for the **[DFRobot Beetle ESP32-C6](https://wiki.dfrobot.com/SKU_DFR1117_Beetle_ESP32_C6)** - a tiny 25mm × 20.5mm board that's perfect for embedding in LED strip projects. Any ESP32-C6 board should work, but the Beetle's small size makes it ideal.

### Requirements

- **ESP32-C6 board** - DFRobot Beetle ESP32-C6 recommended (or any ESP32-C6)
- **Addressable LED strip** - WS2812B, WS2811, or SK6812 (RGBW)
- **5V power supply** - Size for your LED count (~60mA per LED at full white)
- **Thread border router** - HomePod Mini, Apple TV 4K, Google Nest Hub, or dedicated like SLZB-06/SMLight

### 3D Printable Enclosure

A parametric OpenSCAD enclosure design is included in the `enclosure/` folder, sized specifically for the DFRobot Beetle ESP32-C6.

<p align="center">
  <img src="images/enclosure_render.png" alt="Enclosure render" width="400">
  <img src="images/enclosure_photo.png" alt="Printed enclosure" width="300">
</p>

**Features:**
- **Friction-fit lid** - No screws needed, snaps securely in place
- **USB-C port cutout** - Easy access for flashing and power
- **Wire slit** - Solder your wires first, then slide them into the enclosure
- **Super compact** - Just slightly larger than the Beetle board itself

Ready-to-print STL files (`tled_base.stl`, `tled_lid.stl`) are included in the `enclosure/` folder. If you want to tweak dimensions, open `tled_enclosure.scad` in [OpenSCAD](https://openscad.org/) - it's fully parametric so you can adjust wall thickness, tolerances, and ventilation hole sizes.

**Printing tips:**
- Print the base upside-down (opening facing up)
- 0.2mm layer height works well
- No supports needed
- PLA or PETG recommended

## Quick Start

### 1. Flash the Firmware

Visit the **[Web Installer](https://maui1911.github.io/TLED)** and click "Install TLED Firmware".

> **Note:** Requires Chrome or Edge browser. If prompted, hold the BOOT button on your ESP32-C6 while clicking Install.

### 2. Configure Your LED Strip

1. Go to the **Configure** tab in the web installer
2. Click **Connect to Device**
3. Set your LED count, GPIO pin, and LED type
4. Click **Save & Reboot**

### 3. Commission to Your Smart Home

After reboot, a QR code will appear in the web installer. Scan it with:
- **Home Assistant** - Settings → Devices & Services → Add Integration → Matter
- **Apple Home** - Add Accessory → Scan QR Code
- **Google Home** - Add Device → Matter-enabled device

## Configuration Options

| Setting | Default | Description |
|---------|---------|-------------|
| LED Count | 10 | Number of LEDs in your strip (1-1000) |
| GPIO Pin | 5 | Data pin connected to LED strip |
| LED Type | WS2812B | Chipset: WS2812B, WS2811, or SK6812 (RGBW) |
| RGB Order | GRB | Color byte order (try others if colors are wrong) |
| Max Brightness | 255 | Limits maximum brightness (saves power) |
| Device Name | TLED | Name shown in your smart home app |
| Power-on | restore | Behavior on power up: restore last state, on, or off |

## Wiring

```
ESP32-C6          LED Strip
─────────         ─────────
GPIO 5    ────────  DIN (Data In)
GND       ────────  GND
                    5V  ──── External 5V Power Supply
```

> **Important:** Power your LED strip from an external 5V supply, not from the ESP32's 5V pin (except for very short strips).

## Serial Commands

Connect via USB and use the serial console in the web installer, or any serial terminal at 115200 baud:

```
help                    Show available commands
config                  Show current configuration
set leds <n>            Set number of LEDs (1-1000)
set gpio <n>            Set data GPIO pin
set type <type>         Set LED type (ws2812b/ws2811/sk6812)
set order <order>       Set RGB order (grb/rgb/brg/rbg/bgr/gbr)
set brightness <1-255>  Set max brightness
set name <name>         Set device name
set poweron <mode>      Power-on behavior (restore/on/off)
save                    Save configuration and reboot
reboot                  Restart device
factory                 Factory reset (erases settings & commissioning)
```

## Building from Source

### Prerequisites

- [ESP-IDF v5.4+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/)
- [ESP-Matter](https://github.com/espressif/esp-matter)

### Build & Flash

```bash
# Source the environments
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh

# Build
idf.py build

# Flash (keeps commissioning data)
idf.py -p /dev/ttyACM0 flash

# Flash with erase (clears commissioning - need to re-pair)
idf.py -p /dev/ttyACM0 erase-flash flash

# Monitor serial output
idf.py -p /dev/ttyACM0 monitor
```

### Configuration via menuconfig

```bash
idf.py menuconfig
# Navigate to "TLED Configuration" for build-time defaults
```

## Troubleshooting

### Can't flash the device
- Use Chrome or Edge (Firefox doesn't support Web Serial)
- Hold the BOOT button while clicking Install
- Try a different USB cable (some are charge-only)

### "No bootable app partitions" / Boot loop
- Flash was interrupted. Try flashing again.

### Can't find device during commissioning
- Commission within 30 seconds of boot (BLE advertising slows down)
- Run `factory` command if device was previously commissioned
- Move closer to your Thread border router

### Wrong colors
- Try different RGB Order settings (GRB → RGB → BGR → RBG)

### LEDs don't light up
- Check 5V power supply connection
- Verify GPIO pin matches your wiring
- Confirm LED count is correct

## Project Structure

```
TLED/
├── main/
│   ├── app_main.cpp            # Matter setup, endpoint creation
│   ├── app_driver.cpp          # LED strip driver, transitions, effects
│   ├── app_nvs_config.cpp      # Runtime configuration storage
│   ├── app_serial_config.cpp   # USB serial command interface
│   ├── app_monitoring.cpp      # Health monitoring, watchdog, temperature
│   ├── app_device_info.cpp     # Matter device branding
│   ├── app_ble_config.cpp      # BLE commissioning configuration
│   └── Kconfig.projbuild       # Build-time configuration options
├── web-installer/
│   ├── index.html              # Web installer & configurator
│   └── manifest.json           # ESP Web Tools manifest
├── enclosure/
│   ├── tled_enclosure.scad     # OpenSCAD parametric enclosure design
│   ├── tled_base.stl           # Pre-exported base STL
│   └── tled_lid.stl            # Pre-exported lid STL
├── partitions.csv              # Flash partition layout
└── sdkconfig.defaults          # Default SDK configuration
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- [ESP-Matter](https://github.com/espressif/esp-matter) - Espressif's Matter SDK
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/) - Browser-based flashing
- [ConnectedHomeIP](https://github.com/project-chip/connectedhomeip) - Matter protocol implementation
