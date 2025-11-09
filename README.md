# Hardware

Kiln (comsol/solidworks)

Controller (PCBs/electrical wiring diagram)


# Simulated temperature

Capability to use ODE, see "sensor_task.cpp"-> void readSimulatedTemp()

Modify following line in userSetup.h to change:

const bool SIMULATION = false;           // Uses First Order model simulation instead of real input/outputs

# ESP32 Electric Kiln Controller

![Kiln Controller Demo](URL_TO_YOUR_DEMO_IMAGE_OR_GIF_HERE)

This project is an open-source, DIY controller for electric kilns. It uses an ESP32 microcontroller to provide precise temperature control, a TFT display + web interface for creating and managing firing schedules, and real-time data logging to InfluxDB for visualization in Grafana.

## Features

* **PID Temperature Control:** Accurate and stable temperature regulation for your kiln.
* **Multi-segment Firing Programs:** Create complex firing schedules with up to 20 segments, each with a target temperature, firing rate, and hold time.
* **Web Interface:** Configure WiFi, create, and manage firing programs through a user-friendly web interface hosted on the ESP32.
* **Real-time Data Logging:** Pushes temperature data to InfluxDB for live monitoring and analysis.
* **Safety Features:** Includes a door-mounted limit switch to shut down the kiln if the door is opened.
* **Simulation Mode:** Test firing programs without actually heating the kiln.

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

* **PCB:** The PCB design files can be found in the `/hardware` directory. I've included the Gerber files and the original design files (e.g., from KiCad or Eagle).
* **Wiring Diagram:** For a clear guide on how to connect all the components, please refer to the wiring diagram in the `/hardware` directory.

Here's how the ESP32 pins are connected in this project:

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

* `wollewald/ADS1220_WE @ 1.0.15`
* `br3ttb/PID @ 1.2.1`
* `tobiasschuerg/ESP8266 Influxdb @ 3.13.1`
* `me-no-dev/AsyncTCP @ 1.1.1`
* `bblanchon/ArduinoJson @ 6.21.4`
* `https://github.com/me-no-dev/ESPAsyncWebServer.git`

### Installation Steps

1.  **Clone the repository:**
    ```bash
    git clone [https://github.com/your-username/your-repo-name.git](https://github.com/your-username/your-repo-name.git)
    cd your-repo-name
    ```

2.  **Configure your secrets:**
    * In the `src` directory, you'll find a file named `secrets.h.template`.
    * Make a copy of this file and rename it to `secrets.h`.
    * Open `src/secrets.h` and fill in your InfluxDB credentials.

3.  **Upload the filesystem:**
    * The web interface files are stored in the `/data` directory.
    * In PlatformIO, run the "Upload Filesystem Image" task. This will upload the HTML, CSS, and JavaScript files to the ESP32's SPIFFS.

4.  **Build and Upload:**
    * Connect your ESP32 to your computer.
    * In PlatformIO, click the "Upload" button.

## Configuration and Usage

### WiFi Setup

1.  The first time you power on the controller, it will start in Access Point (AP) mode.
2.  Connect to the WiFi network named **"The Kiln Controller"**.
3.  Once connected, a captive portal should open automatically. If not, open a web browser and navigate to `http://192.168.4.1`.
4.  From the web interface, you can scan for and connect to your local WiFi network.

### Creating Firing Programs

1.  Once connected to your local network, you can access the controller's web interface by navigating to its IP address in your web browser.
2.  The web interface allows you to create and save multiple firing programs.

## InfluxDB and Grafana Setup

**(You need to add instructions on how to set up InfluxDB and Grafana here.)**

This project can send data to InfluxDB for real-time monitoring. Here's how to set it up:

1.  **InfluxDB:**
    * Sign up for an InfluxDB Cloud account or set up a local instance.
    * Create a bucket for your kiln data.
    * Generate an API token with write access to your bucket.
    * Enter these details into your `src/secrets.h` file.

2.  **Grafana:**
    * Connect Grafana to your InfluxDB instance as a data source.
    * I've included a sample Grafana dashboard configuration in the `/grafana` directory. You can import this JSON file to get started quickly.

## Contributing

Contributions are welcome! If you have any ideas, suggestions, or improvements, please feel free to open an issue or submit a pull request.

## License

**(Choose a license for your project. The MIT license is a popular choice for open-source projects.)**

This project is licensed under the MIT License. See the `LICENSE` file for details.