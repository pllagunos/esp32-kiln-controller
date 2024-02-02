#ifndef COMMON_H
#define COMMON_H

#include <Arduino.h>
#include "userSetup.h"

struct FiringSegment {
  int targetTemperature; // Target temp for each segment (degrees).
  int firingRate; // Rate of temp change for each segment (deg/hr).
  int holdingTime; // Hold time for each segment (min).  This starts after it reaches target temp
};

struct FiringProgram {
  int programNumber;
  String name;
  String duration;
  String createdDate;
  FiringSegment segments[maxSegments];
  int segmentQuantity;
};

extern FiringProgram currentProgram; // Current firing program loaded into memory
extern SemaphoreHandle_t mutex;      // For thread safety
extern SemaphoreHandle_t disp_mutex; // For display calls

/* global variables: used between concurrent tasks -> mutex */
extern double g_pidInput;            // Input for PID loop (actual temp reading from tc).
extern double g_pidOutput;           // Output for PID loop (relay for heater).
extern double g_pidSetPoint;         // Setpoint for PID loop (temp you are trying to reach).
extern int g_segNum;                 // Current segment number running in firing program.  0 means a program hasn't been selected yet.
extern bool g_connected;             // Is the ESP connected to WiFi?
extern bool g_published;             // Is the ESP publishing to InfluxDB?
extern bool g_fault;                 // Is there a fault that needs to be known across threads?

#endif