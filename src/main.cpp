#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>               
#include <SPIFFS.h>    

#include "userSetup.h"      // Setup user variables (CHANGE THESE IN HEADER FILE)
#include "common.h"         // Common variables and functions

#include "gui.h"            // Graphical user interface source file
#include "heat_control.h"   // PID and heating control code
#include "network.h"        // WiFi and Server related code
#include "database_task.h"  // Influx DB publishing task
#include "sensor_task.h"    // Thermocouple reading task

// global (shared) variables definition
double g_pidInput;
double g_pidOutput;
double g_pidSetPoint;
int g_segNum;
bool g_connected;
bool g_published;

// External objects initialization
FiringProgram currentProgram;
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
heat_control controller(mutex);
Network network(mutex, SPIFFS);

// put function declarations here:
void main_task(void* parameter);
void resetCheck();

void setup() {
  Serial.begin(115200);
  SPI.begin();
  delay(250);

  gui_start();

  // Mount SPIFFS file system
  while (!SPIFFS.begin(true)) {
    disp_error_msg("SPIFFS Error", "Can't setup file system.", "Make sure files are uploaded.");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }

  network.initWiFi();

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

  log_i("Total heap: %d", ESP.getHeapSize());
  log_i("Free heap: %d", ESP.getFreeHeap());
}

// Task function to perform main Kiln control on core 1
void main_task(void* parameter) {

  while (1) {
    
    controller.run();
    
    gui_run();

    resetCheck();
  }
}

//*******************************************************************************************************************************

// Reset button: 200 ms press for TFT, 2s press for system
void resetCheck() {
  if (digitalRead(rstPin) == LOW) {
    unsigned long resetStart = millis();
    while (digitalRead(rstPin) == LOW) {   // Wait for the button to be released
      if (millis() - resetStart > 2000) {  // reset system
        controller.shutDown();
        esp_restart();
      }
    }
    unsigned long pressTime = millis() - resetStart;  // Calculate how long the button was pressed

    if (pressTime >= 2000) {  // If pressed for 2 seconds or more, reset the system
      controller.shutDown();
      esp_restart();
    } else if (pressTime >= 200) resetTFT();  // If pressed for 200 ms or more, reset the display
  }
}

void loop() {
}
