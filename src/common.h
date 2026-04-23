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

enum class FiringModes {
  automatic,
  manual
};

struct InfluxDbConfig {
  String url;
  String token;
  String org;
  String bucket;
  String tzInfo;
  bool configured = false;
};

enum class OtaStatus {
  IDLE,
  CHECKING,
  UPDATE_AVAILABLE,
  UP_TO_DATE,
  UPDATING,
  ERROR
};

extern FiringProgram currentProgram; // Current firing program loaded into memory
extern InfluxDbConfig g_influxConfig; // InfluxDB connection settings loaded from SPIFFS
extern OtaStatus g_ota_status;        // OTA state machine status
extern String g_ota_latest_version;   // Latest release name from GitHub
extern String g_ota_latest_tag;       // Latest release tag from GitHub
extern SemaphoreHandle_t mutex;        // For thread safety
extern SemaphoreHandle_t disp_mutex;   // For display calls
extern SemaphoreHandle_t g_spiMutex;   // SPI bus mutex for thermocouple chip transactions

/* global variables: used between concurrent tasks -> mutex */
extern double g_pidInput;              // Input for PID loop (actual temp reading from tc).
extern double g_pidOutput;             // Output for PID loop (relay for heater).
extern double g_pidSetPoint;           // Setpoint for PID loop (temp you are trying to reach).
extern int g_segNum;                   // Current segment number running in firing program.
extern bool g_connected;               // Is the ESP connected to WiFi?
extern bool g_connecting;              // Is the ESP trying to connect to WiFi
extern bool g_published;               // Is the ESP publishing to InfluxDB?
extern char g_tcType;                  // Runtime thermocouple type (single char: B E J K N R S T)
extern bool g_tcInitialized;           // True when the thermocouple driver is ready to read
extern String g_errMsg;                // Human-readable init or runtime fault message
extern bool g_tcFault;                 // Active thermocouple fault (safety-critical)
extern uint8_t g_tcFaultCode;          // Raw MAX31856 fault bitmask (0 for ADS1220)

#endif