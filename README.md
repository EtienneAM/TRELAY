# TRELAY - Matter-over-Thread Relay Controller

A Matter-compatible relay controller for ESP32-C6 that works over Thread networking. Control any relay, solenoid, or dry-contact load from Home Assistant, Apple Home, Google Home, or any Matter-compatible smart home platform — no WiFi, no cloud required.

Originally designed as a **door buzzer controller**: commission it as a Door Lock, press Unlock in Home Assistant, the relay energizes for a few seconds, then automatically re-locks.

> **Disclaimer:** This entire firmware was written by AI ([Claude](https://claude.ai) by Anthropic). I ([@EtienneAM](https://github.com/EtienneAM)) have not written a single line of code — I only provided direction, built the firware, tested on real hardware, and deployed. Use at your own risk.

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

Developed and tested for the **[Waveshare ESP32-C6-DEV-KIT-N8](https://docs.waveshare.com/ESP32-C6-DEV-KIT-N8)**. Any ESP32-C6 board works.

### Requirements

- **ESP32-C6 board** - Any ESP32-C6, I used the **[Waveshare ESP32-C6-DEV-KIT-N8](https://docs.waveshare.com/ESP32-C6-DEV-KIT-N8)**
- **Relay module** - Any 3.3 V or 5 V trigger relay (active-high or active-low). I used the **[Adafruit Non-Latching Mini Relay FeatherWing](https://www.adafruit.com/product/2895)**
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

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- [ESP-Matter](https://github.com/espressif/esp-matter) - Espressif's Matter SDK
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/) - Browser-based flashing
- [ConnectedHomeIP](https://github.com/project-chip/connectedhomeip) - Matter protocol implementation
