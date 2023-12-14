#ifndef USERSETUP_H
#define USERSETUP_H

#include "Adafruit_MAX31856.h"  // Thermocouple card library
#include "PID_v1.h"

// Setup user variables (CHANGE THESE TO MATCH YOUR SETUP)
const int tempCycle = 2500;              // Temperature reading cycle
const int maxTemp = 1300;                // Maximum temperature (degrees).  If reached, will shut down.
const int heatingCycle = 2500;           // Time for a complete PID on/off cycle for the heating elements (ms)
double Kp = 800, Ki = 47.37, Kd = 4.93;  // PID constants (tunings), VERY IMPORTANT TO GET THEM RIGHT.
const int tempOffset = 0;                // Temp offset (degrees) of thermocouple, either from a cold zone or exernal factors. This is added to the setpoint.
const int tempRange = 2;                 // This is how close the temp reading needs to be to the set point to shift to the hold phase (degrees).  Set to zero or a positive integer.
const char tempScale = 'C';              // Temperature scale.  F = Fahrenheit.  C = Celsius
const int WiFi_refresh = 4000;           // Refresh rate to update WiFi quality (ms)
max31856_thermocoupletype_t TCTYPE = MAX31856_TCTYPE_S;
const int MAX_SEGMENTS = 20;             // Maximum number of segments in a firing program
// // Network credentials: 
// const char* network1 = "networkSSID";  //"networkSSID";
// const char* pwd1 = "password";        //"password";
// const char* network2 = "networkSSID";
// const char* pwd2 = "password";
// const char* network3 = "networkSSID";
// const char* pwd3 = "password";
// //* fill the network arrays */
// const char* network[] = {network1, network2, network3};
// const char* password[] = {pwd1, pwd2, pwd3};

// Setup pin connections (CHANGE THESE TO MATCH YOUR SETUP)
const int upPin = 16;           // Pin # connected to up arrow button #1
const int selectPin = 17;       // Pin # connected to select / start button #2
const int downPin = 21;         // Pin # connected to down arrow button #3
const int heaterPin = 27;       // Pin # connected to relay of heating element.
const int thermocoupleCS = 33;  // CS pin for thermocouple. SPI is hardware such that: DO -> MISO (19), CLK -> SCLK (18)
const int limitSwitchPin = 25;  // Pin # connected to safety limit switch mounted on kiln door
const int rstPin = 22;          // Pin # connected to reset button
const int ledPin = 2;
/* temporary SPI pins for thermocouple */
const int MAX_CLK = 13;
const int MAX_DO = 14;
const int MAX_DI = 26;

// TFT settings
#define bar_color 0x53D2  // Color for top bar
#define tftwidth 320      // pixel width
#define tftheight 240     // pixel height

// InfluxDB settings

// InfluxDB v2 server url, e.g. https://eu-central-1-1.aws.cloud2.influxdata.com (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "INFLUX-URL"
// InfluxDB v2 server or cloud API token (Use: InfluxDB UI -> Data -> API Tokens -> <select token>)
#define INFLUXDB_TOKEN "REDACTED_TOKEN"
// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG "INFLUX_ORG"
// InfluxDB v2 bucket name (Use: InfluxDB UI ->  Data -> Buckets)
#define INFLUXDB_BUCKET "INFLUX_BUCKET"
// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
#define TZ_INFO "CST+6CDT,M4.1.0/2,M10.5.0/2" //"CET-1CEST,M3.5.0,M10.5.0/3"

#endif