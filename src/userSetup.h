#ifndef USERSETUP_H
#define USERSETUP_H

#include <Arduino.h>

const bool SIMULATION = false;           // Uses First Order model simulation instead of real input/outputs
const double alpha = 30;                 // 1s sim = 30s program

// Setup user variables (CHANGE THESE TO MATCH YOUR SETUP)
const int tempCycle = 500;               // Temperature reading cycle
const int maxTemp = 1300;                // Maximum temperature (degrees).  If reached, will shut down.
const int heatingCycle = 2500;           // Time for a complete PID on/off cycle for the heating elements (ms)
inline double Kp = 800;                  // PID proportional gain
inline double Ki = 47.37;                // PID integral gain 
inline double Kd = 4.93;                 // PID derivative gain
const int tempOffset = 0;                // Temp offset (degrees) of thermocouple, either from a cold zone or exernal factors. This is added to the setpoint.
const int tempRange = 2;                 // This is how close the temp reading needs to be to the set point to shift to the hold phase (degrees).  Set to zero or a positive integer.
const char tempScale = 'C';              // Temperature scale.  F = Fahrenheit.  C = Celsius
const int topBarCycle = 4000;            // Refresh rate to update top info bar on TFT (ms)
const int maxSegments = 20;              // Maximum number of segments in a firing program
const int LONG_PRESS_TIME = 500;         // Time in milliseconds that defines a long press
// Thermocouple driver selection — uncomment exactly one:
#define TC_DRIVER_ADS1220
// #define TC_DRIVER_MAX31856

#if defined(TC_DRIVER_ADS1220) == defined(TC_DRIVER_MAX31856)
#error "Define exactly one of TC_DRIVER_ADS1220 or TC_DRIVER_MAX31856 in userSetup.h"
#endif

constexpr char TC_DEFAULT_TYPE = 'S'; // Default thermocouple type (B E J K N R S T)

// Setup pin connections (CHANGE THESE TO MATCH YOUR SETUP)
const int upPin = 16;               // Pin # connected to up arrow button #1
const int selectPin = 17;           // Pin # connected to select / start button #2
const int downPin = 21;             // Pin # connected to down arrow button #3
const int heaterPin = 27;           // Pin # connected to SSR of heating element.
const int relayPin = 26;            // Pin # connected to main relay
const int limitSwitchPin = 25;      // Pin # connected to safety limit switch mounted on kiln door
const int rstPin = 22;              // Pin # connected to reset button
constexpr int TC_CS_PIN   = 33;     // Thermocouple chip select (MAX31856 / ADS1220)
constexpr int TC_DRDY_PIN = 13;     // ADS1220 data-ready (unused with MAX31856)

// TFT settings
#define bar_color 0x53D2  // Color for top bar
#define tftwidth 320      // pixel width
#define tftheight 240     // pixel height

#endif