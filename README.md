# ESP32 Electric Kiln Controller

![Kiln Controller Demo](URL_TO_YOUR_DEMO_IMAGE_OR_GIF_HERE)

This project is an open-source, DIY controller for electric kilns. It uses an ESP32 microcontroller to provide precise temperature control, a TFT display + web interface for creating and managing firing schedules, real-time data logging to InfluxDB for visualization in Grafana, and over-the-air (OTA) firmware updates via GitHub Releases.

## Features

* **PID Temperature Control:** Accurate and stable temperature regulation for your kiln.
* **Multi-segment Firing Programs:** Create complex firing schedules with up to 20 segments, each with a target temperature, firing rate, and hold time.
* **Captive Portal Web Interface:** Configure WiFi, InfluxDB credentials, and firing programs through a web interface hosted directly on the ESP32 — no app required. Accessible at `http://192.168.4.1` when in AP mode.
* **InfluxDB Configuration via Web UI:** Enter and update your InfluxDB credentials (URL, API token, org, bucket, timezone) through the web interface. Credentials are saved to SPIFFS and survive reboots — no recompile needed.
* **OTA Firmware Updates:** Check for and install firmware updates directly from the web interface. Updates are pulled from GitHub Releases automatically when you publish a new release.
* **Real-time Data Logging:** Pushes temperature data to InfluxDB for live monitoring and analysis in Grafana.
* **Safety Features:** Includes a door-mounted limit switch to shut down the kiln if the door is opened.
* **Simulation Mode:** Test firing programs without actually heating the kiln.

# Hardware

Kiln (comsol/solidworks)

Controller (PCBs/electrical wiring diagram)

# Simulated temperature

Capability to use ODE, see "sensor_task.cpp"-> void readSimulatedTemp()

Modify following line in userSetup.h to change:

```cpp
const bool SIMULATION = false;           // Uses First Order model simulation instead of real input/outputs
```

## Hardware Requirements

Below is a list of the components I used for this project.

### Electronics

* **Microcontroller:** ESP32-DOIT-DEVKIT-V1
* **Thermocouple Amplifier:** An ADS1220-based board for accurate temperature readings.
* **Thermocouple:** Type-S Thermocouple (but configurable for B, K, R, N, E, J, and T types).
* **Solid State Relay (SSR):** To control the heating elements.
* **Main Relay:** For main power control.
* **TFT Display:** A 320x240 pixel display for the user interface.
* **Buttons:** Three push buttons for on-device navigation (Up, Select, Down).
* **Limit Switch:** A safety switch for the kiln door.
* **Reset Button:** A reset button for the ESP32.

### PCB and Wiring

**(You need to add your PCB files and a wiring diagram here.)**

* **PCB:** The PCB design files can be found in the `/hardware` directory.
* **Wiring Diagram:** For a clear guide on how to connect all the components, please refer to the wiring diagram in the `/hardware` directory.

| Component             | ESP32 Pin |
| --------------------- | --------- |
| Up Button             | 16        |
| Select Button         | 17        |
| Down Button           | 21        |
| Heating Element SSR   | 27        |
| Main Relay            | 26        |
| Limit Switch          | 25        |
| Reset Button          | 22        |
| Thermocouple CS       | 33        |
| Thermocouple DRDY     | 13        |

## Software and Installation

This project is built using [PlatformIO](https://platformio.org/).

### Prerequisites

* [Visual Studio Code](https://code.visualstudio.com/)
* [PlatformIO IDE Extension](https://platformio.org/platformio-ide)

### Dependencies

The following libraries are used in this project and will be installed automatically by PlatformIO:

* `bodmer/TFT_eSPI @^2.5.43`
* `wollewald/ADS1220_WE @ 1.0.15`
* `br3ttb/PID @ 1.2.1`
* `tobiasschuerg/ESP8266 Influxdb @ 3.13.1`
* `me-no-dev/AsyncTCP @ 1.1.1`
* `me-no-dev/ESPAsyncWebServer @ 1.2.4`
* `bblanchon/ArduinoJson @ 6.21.4`

### Installation Steps

1. **Clone the repository:**
   ```bash
   git clone https://github.com/pllagunos/ElectricKiln.git
   cd ElectricKiln
   ```

2. **Upload the filesystem:**
   * The web interface files are stored in the `/data` directory.
   * In PlatformIO, run the **"Upload Filesystem Image"** task. This uploads the HTML, CSS, and JavaScript files to the ESP32's SPIFFS partition.
   * ⚠️ **This step is required before first use.** The device cannot serve the web UI without it.

3. **Build and Upload:**
   * Connect your ESP32 to your computer.
   * In PlatformIO, click the **"Upload"** button (or run `pio run --target upload`).

## Configuration and Usage

### First Boot — WiFi Setup

1. On first power-on the controller starts in **Access Point (AP) mode**.
2. Connect your phone or computer to the WiFi network named **"The Kiln Controller"**.
3. A captive portal opens automatically. If it doesn't, navigate to `http://192.168.4.1`.
4. Click **"WiFi Manager"** → scan for and select your home/studio network → enter the password → Save.
5. The device will reboot and connect to your WiFi. Its assigned IP address will appear on the TFT display.

### InfluxDB Setup (Optional — for data logging)

InfluxDB credentials are configured entirely at runtime through the web UI — no recompile needed.

1. Open the captive portal (AP mode) or navigate to the device's IP address on your local network.
2. Click **"InfluxDB Manager"**.
3. Fill in:
   * **URL** — your InfluxDB instance, e.g. `https://us-east-1-1.aws.cloud2.influxdata.com`
   * **Token** — an API token with write access to your bucket
   * **Org** — your InfluxDB organization name or email
   * **Bucket** — the bucket to write data to
   * **Timezone** — POSIX tz string, e.g. `CST+6CDT,M4.1.0/2,M10.5.0/2`
4. Click **Save**. Credentials are stored in SPIFFS and survive reboots.

> **Note:** The API token is never sent back to the browser after saving — only the URL, org, bucket, and timezone are pre-filled when you revisit the form.

To set up InfluxDB Cloud and Grafana for visualization:
1. Sign up at [influxdata.com](https://www.influxdata.com/) and create a bucket.
2. Generate an API token with write access.
3. In Grafana, add InfluxDB as a data source and import the sample dashboard from `/grafana` (if included).

### OTA Firmware Updates

The device can check for and install firmware updates directly from GitHub Releases — no USB cable needed after initial flashing.

**How it works:**
* When a new GitHub Release is published, the CI pipeline automatically builds the firmware, attaches `esp32doit-devkit-v1_firmware.bin` to the release, and appends the firmware's MD5 hash to the release notes.
* The device compares the current `OTA_VERSION` build flag against the release **tag** (e.g. `v1.0.1`).
* If a newer version is available, the "Update Now" button appears.
* Firmware integrity is verified with an MD5 hash fetched from the GitHub API before flashing.

**To check for updates:**
1. Make sure the device is connected to WiFi (not in AP mode).
2. Navigate to the device's IP address on your local network → **"Firmware Update"**.
3. Click **"Check for Update"**. The device queries `api.github.com`.
4. If an update is found, click **"Update Now"**. The device downloads, verifies, flashes, and restarts automatically (~60 seconds).
5. ⚠️ **Do not power off the device during an update.**

**To publish a new release (for developers):**
1. Go to the GitHub repo → Releases → "Draft a new release".
2. Create a tag (e.g. `v1.0.1`). The **Release title** can be anything — the device compares against the tag, not the title.
3. Click "Publish release". GitHub Actions will build and attach the firmware binary, and append the MD5 hash to the release notes automatically (takes ~2–3 minutes).

### Creating Firing Programs

1. Open the captive portal or navigate to the device IP on your local network.
2. Click **"Program Editor"** to create and save firing programs.
3. Each segment defines a **target temperature**, **firing rate (°C/hr)**, and **hold time (min)**.

## Contributing

Contributions are welcome! Feel free to open an issue or submit a pull request.

## To Do
- Autotune PID ala https://github.com/hirschmann/pid-autotune/blob/master/autotune.py

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.