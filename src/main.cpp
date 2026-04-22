#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>               
#include <SPIFFS.h>    

#include "userSetup.h"      // Setup user variables (CHANGE THESE IN HEADER FILE)
#include "common.h"         // Common variables and functions

#include "gui.h"            // Graphical user interface source file
#include "heat_control.h"   // PID and heating control code
#include "network.h"        // WiFi and Server related code
#include "ota_task.h"       // OTA firmware update task
#include "database_task.h"  // Influx DB publishing task
#include "sensor_task.h"    // Thermocouple reading task

// global (shared) variables definition
double g_pidInput;
double g_pidOutput;
double g_pidSetPoint;
int g_segNum;
bool g_connected;
bool g_connecting;
bool g_published;
char g_tcType;
bool g_tcInitialized;
char g_initErr[64];
bool g_tcFault;
uint8_t g_tcFaultCode;

// External objects initialization
FiringProgram currentProgram;
InfluxDbConfig g_influxConfig;
OtaStatus g_ota_status = OtaStatus::IDLE;
String g_ota_latest_version;
String g_ota_latest_tag;
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t disp_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t g_spiMutex = xSemaphoreCreateMutex();
heat_control controller(mutex);
Network network(mutex, SPIFFS);

// put function declarations here:
void main_task(void* parameter);
void resetCheck();

void setup() {
  Serial.begin(115200);
  SPI.begin();

  delay(1500);
  gui_start(); // also seeds g_tcType from preferences (falls back to TC_DEFAULT_TYPE)

  // Mount SPIFFS file system
  while (!SPIFFS.begin(true)) {
    disp_error_msg("SPIFFS Error", "Can't setup file system.", "Make sure files are uploaded.");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }

  // Load WiFi credentials
  network.loadWifiCredentials();
  // Load InfluxDB credentials
  network.loadInfluxDbCredentials();

  // Pin modes
  pinMode(heaterPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(limitSwitchPin, INPUT_PULLUP); // should it be an interrupt?
  
  // Create the main task and set its affinity to core 1
  xTaskCreatePinnedToCore(main_task, "Main", 8192, NULL, 1, NULL, 1);
  // Create the sensor task and set its affinity to core 1
  xTaskCreatePinnedToCore(sensor_task, "Sensor", 8192, NULL, 1, NULL, 1); 
  // Create the publishing task and set its affinity to core 0
  xTaskCreatePinnedToCore(database_task, "Database", 32768, NULL, 1, NULL, 0);
  // Create the OTA task and set its affinity to core 0
  xTaskCreatePinnedToCore(ota_task, "OTA", 32768, NULL, 1, NULL, 0);

  log_i("Total heap: %d", ESP.getHeapSize());
  log_i("Free heap: %d", ESP.getFreeHeap());
}

// Task function to perform main Kiln control on core 1
void main_task(void* parameter) {

  while (1) {
    
    controller.run();
    
    gui_run();

  }
}

//*******************************************************************************************************************************

void loop() {
  vTaskDelay(10);
}
