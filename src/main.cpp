// Libraries to include
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>           // Graphics and font library for ILI9341 driver chip
#include "Adafruit_MAX31856.h"  // thermocouple card library (56)
#include <PID_v1.h>             // PID temp control library
#include <SPI.h>                // Serial Peripheral Interface library
#include <SD.h>                 // SD memory card library (SPI is required)
#include <FS.h>                 // So that ESP32 recognizes SD card
#include <WiFiMulti.h>          // Enables conection to multiple WiFi networks
#include <InfluxDbClient.h>     // Write data to Influx Data Base
#include <InfluxDbCloud.h>      // Enable Influx Data Cloud storage
#include <Preferences.h>        // library to save variables to EPROOM

//****************************
// VARIABLES
//****************************

// Setup user variables (CHANGE THESE IN HEADER FILE)
// #include "../src/userSetup.h"
#include "userSetup.h"

// Internal variables
unsigned long TFT_start;     // Exact time you refreshed the TFT screen (ms).  Based on millis().
unsigned long tempStart;     // Exact time you last updated the temperature (ms). Based on millis().
unsigned long WiFi_start;    // Exact time you refreshed the WiFi quality symbol (ms).
unsigned long Influx_start;  // Exact time you last uploaded data to Influx DB (ms).
unsigned long programStart;  // Exact time you started running the program (ms).  Based on millis().
unsigned long heatStart;     // Exact time you started the new heating cycle (ms).  Based on millis().
unsigned long rampStart;     // Exact time the ramp phase of the segment started (ms).  Based on millis().
unsigned long holdStart;     // Exact time the hold phase of the segment started (ms).  Based on millis().
double pidInput;             // Input for PID loop (actual temp reading from thermocouple).  Don't change.
double pidInput_global;      // "" but for both tasks
double pidOutput;            // Output for PID loop (relay for heater).  Don't change.
double pidOutput_global;     // "" but for both tasks
double pidSetPoint;          // Setpoint for PID loop (temp you are trying to reach).  Don't change.
double pidSetPoint_global;   // "" but for both tasks
double calcSetPoint;         // Calculated set point (degrees)
double calcSetPoint_global;  // "" but for both tasks
double rampHours;            // Time it has spent in ramp (hours)
double lastRampHours;        // Last saved elapsed time of ramp (hours)
double holdMins;             // Time it has spent in hold (mins)
double lastHoldMins;         // Last saved elapsed time in hold (mins)
char programDesc1[21];       // Program description #1 (first line of text file)
char* screen = "intro";      // Variable that holds screen type (start with intro)
int introSel = 1;            // Intro menu selected option (start or settings)
int confirmSel;              // Confirm selected option (back or OK)
int settingsSel = 1;         // Settings menu selected setting
int segQuantity;             // Last segment number in firing program
int lastTemp;                // Last setpoint temperature (degrees)
int optionNum = 1;           // Option selected from screen #3
int programNumber;           // Current firing program number.  This ties to the file name (ex: 1.txt, 2.txt).
int screenNum = 1;           // Screen number displayed during firing (1 = temps / 2 = program info / 3 = tools / 4 = done
int segNum = 0;              // Current segment number running in firing program.  0 means a program hasn't been selected yet.
int segNum_global = 0;       // "" but for both tasks
int segHold[20];             // Hold time for each segment (min).  This starts after it reaches target temp.
int segRamp[20];             // Rate of temp change for each segment (deg/hr).
int segTemp[20];             // Target temp for each segment (degrees).
int action;                  // Action methods
int actionSel = 1;           // Option selected from action screen
bool programOK = false;      // Is the program you loaded OK?
bool isOnHold = 0;           // Current segment phase.  0 = ramp.  1 = hold.
bool doorClosed = true;      // Kiln door has a limit switch attached to it.
bool doorClosed_before;      // To check if door state just changed
bool upPressed = false;      // Up button press state
bool selectPressed = false;  // Select button press state
bool downPressed = false;    // Down button press state
bool influx_OK = false;      // Is InfluxDB publishing working
bool connection_OK = false;  // Is WiFi connection working

//******************************************************************************************************************************
//  SETUP: INITIAL SETUP (RUNS ONCE DURING START)
//******************************************************************************************************************************
/* Initialize stuff */
WiFiMulti wifiMulti;
TFT_eSPI tft = TFT_eSPI();
PID pidCont = { PID(&pidInput, &pidOutput, &pidSetPoint, Kp, Ki, Kd, DIRECT) };
Preferences preferences;
// Adafruit_MAX31856 thermocouple(thermocoupleCS);
Adafruit_MAX31856 thermocouple(thermocoupleCS, MAX_DI, MAX_DO, MAX_CLK);
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point sensor("HORNO ELECTRICO");
SemaphoreHandle_t DataSemaphore;

// put function declarations here:
void publishing_task(void*parameter);
void main_task(void* parameter);
void htrControl();
void shutDown();
void checkDoor();
void updatePIDs();
void updateSeg();
void setupPIDs(int state);
void readTemps();
void runningScreen();
void settingsScreen(int sel);
void introScreen(int sel);
void actionScreen(int actionSel);
void openProgram();
void displayErrorMessage(char* title, char* message1, char* message2);
void initWiFi();
void drawTopBar();
int8_t getWifiQuality();
void resetTFT();
void readButtons();
void btnBounce(int btnPin);
void tftPrintCenterWidth(char* text, int y);
void tftPrint(char* text, int x, int y);

void setup() {
  // Start the serial communication
  Serial.begin(115200);
  SPI.begin();

  // Setup all pin modes on board.
  pinMode(upPin, INPUT_PULLUP);
  pinMode(downPin, INPUT_PULLUP);
  pinMode(selectPin, INPUT_PULLUP);
  pinMode(rstPin, INPUT_PULLUP);
  pinMode(heaterPin, OUTPUT);
  pinMode(limitSwitchPin, INPUT_PULLUP);
  // doorClosed = !digitalRead(limitSwitchPin);  // door is closed when pin is LOW
  // attachInterrupt(digitalPinToInterrupt(limitSwitchPin), checkDoorISR, CHANGE);

  // Setup thermocouple
  thermocouple.begin();
  thermocouple.setThermocoupleType(TCTYPE);  // thermocouple.getThermocoupleType

  tft.init();
  tft.setRotation(3);

  // Setup SD card
  while (!SD.begin()) {
    displayErrorMessage("SD Card Error", "Can't setup SD card.", "Make sure card is in.");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }

  tft.fillScreen(TFT_BLACK);

  // Initialize WiFi
  initWiFi();
  if (WiFi.status() == WL_CONNECTED) timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // setup and retrieve data from EEPROM
  preferences.begin("my-app", false);
  programNumber = preferences.getInt("programNumber", 1);
  action = preferences.getInt("action", 0);  // 0 = X

  // Create the semaphore
  DataSemaphore = xSemaphoreCreateMutex();
  // Create the main task and set its affinity to core 1
  xTaskCreatePinnedToCore(main_task, "Main", 40960, NULL, 1, NULL, 1);
  // Create the publishing task and set its affinity to core 0
  xTaskCreatePinnedToCore(publishing_task, "Publishing", 40960, NULL, 1, NULL, 0);

}

void loop() {
  // Nothing to do here
}

// Task function to publish data on core 0 *** ADD ALL WIFI CONNETING TASKS HERE TOO **
void publishing_task(void* parameter) {
  while (1) {
    // Check WiFi connection
    bool connected = (wifiMulti.run() == WL_CONNECTED);

    xSemaphoreTake(DataSemaphore, portMAX_DELAY);
    connection_OK = connected;
    xSemaphoreGive(DataSemaphore);

    if (connected) {
      sensor.clearFields();
      
      // shared variables: segNum, pidInput, pidSetPoint, pidOutput
      xSemaphoreTake(DataSemaphore, portMAX_DELAY);  // Wait for the semaphore to become available
      sensor.addField("Kiln temperature", (int)pidInput_global);
      sensor.addField("SetPoint", (int)calcSetPoint_global);
      sensor.addField("Output", (int)pidOutput_global);
      sensor.addField("Running", (segNum_global > 0));
      xSemaphoreGive(DataSemaphore);  // Release the semaphore

      // Write data
      bool published = (client.writePoint(sensor));
      if (!published) {
        // Serial.print("InfluxDB write failed: ");
        // Serial.println(client.getLastErrorMessage());
      }

      xSemaphoreTake(DataSemaphore, portMAX_DELAY);
      influx_OK = published;
      xSemaphoreGive(DataSemaphore);
    }
  }
}

// Task function to perform all Kiln control on core 1
void main_task(void* parameter) {
  while (1) {
    //******************************
    // Shutdown if too hot
    if (pidInput >= maxTemp) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextSize(4);
      tftPrintCenterWidth("ERROR", 80);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextSize(2);
      tftPrintCenterWidth("Max temp reached", 150);
      tftPrintCenterWidth("System was shut down.", 180);
      shutDown();
      while (1) {
        if (digitalRead(rstPin) == LOW) esp_restart();
      }
    }
    // Reset button: 200 ms press for TFT, 2s press for system
    if (digitalRead(rstPin) == LOW) {
      unsigned long resetStart = millis();
      while (digitalRead(rstPin) == LOW) {   // Wait for the button to be released
        if (millis() - resetStart > 2000) {  // reset system
          shutDown();
          esp_restart();
        }
      }
      unsigned long pressTime = millis() - resetStart;  // Calculate how long the button was pressed

      if (pressTime >= 2000) {  // If pressed for 2 seconds or more, reset the system
        shutDown();
        esp_restart();
      } else if (pressTime >= 200) resetTFT();  // If pressed for 200 ms or more, reset the display
    }
    // Update temperature
    if (millis() - tempStart >= tempCycle) {
      readTemps();
      tempStart = millis();
    }
    // Update WiFi quality
    if (millis() - WiFi_start >= WiFi_refresh) {
      drawTopBar();
      WiFi_start = millis();
    }

    //******************************
    // Intro screen
    if (segNum == 0 && screen == "intro") {
      readButtons();
      introScreen(introSel);

      if (upPressed && introSel > 1) {
        introSel -= 1;
      }
      if (downPressed && introSel < 2) {
        introSel += 1;
      }

      /* settings */
      if (selectPressed && introSel == 2) {
        screen = "settings";  // user pressed settings
        settingsSel = 1;
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
      }

      /* User pressed START */
      if (selectPressed && introSel == 1) {
        //shift_register.set(gasPin, HIGH);  // allow gas contactor to be manually energized
        //shift_register.set(airPin, HIGH);  // air contactor to be manually energized
        screen = "confirm";                             // go to confirm screen
        confirmSel = 2;                                 // set selection to OK
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tftPrint("  BACK  ", 10, 200);
        tftPrint("> OK <", 200, 200);

        openProgram();
      }
    }
    // Confirm program screen: only refreshes if buttons are pressed, great to avoid EMI from contactors
    if (segNum == 0 && screen == "confirm") {
      readButtons();
      tft.setTextSize(2);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);

      if (upPressed && confirmSel == 2) {
        confirmSel -= 1;
        tftPrint("> BACK <", 10, 200);
        tftPrint("  OK  ", 200, 200);
      }
      if (downPressed && confirmSel == 1) {
        confirmSel += 1;
        tftPrint("  BACK  ", 10, 200);
        tftPrint("> OK <", 200, 200);
      }

      if (selectPressed && confirmSel == 1) {  // pressed back
        //shift_register.set(gasPin, LOW); // block gas contactor from being energized
        //shift_register.set(airPin, HIGH);  // air contactor to be manually energized
        screen = "intro";
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
      }

      if (selectPressed && confirmSel == 2) {  // pressed OK
        segNum = 1;                            // firing
        setupPIDs(HIGH);                       // setup PID algorithm
        lastTemp = pidInput;
        heatStart = millis();
        rampStart = millis();
        programStart = millis();                        // use later to know program duration
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
      }
    }

    //******************************
    // Settings screens
    if (strstr(screen, "settings") != NULL) {

      int settings_options;
      settings_options = 3;  // (select program, action, done)

      // Main settings screen
      if (screen == "settings" && segNum == 0) {
        settingsScreen(settingsSel);  // Display settings screen
        readButtons();

        if (upPressed && settingsSel > 1) settingsSel -= 1;
        if (downPressed && settingsSel < settings_options) settingsSel += 1;

        /* SELECTIONS */
        if (selectPressed) tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear display except top notch
        // user pressed SELECT PROGRAM
        if (selectPressed && settingsSel == 1) {
          screen = "settings_program";
        }
        // user pressed ACTION
        if (selectPressed && settingsSel == 2) {
          screen = "settings_action";
          actionSel = 1;
        }
        // user pressed DONE
        if (selectPressed && settingsSel == 3) {
          screen = "intro";
          introSel = 1;
        }
      }

      // Select program screen
      if (screen == "settings_program" && segNum == 0) {
        // Serial.print("opening program....");
        openProgram();
        // Serial.println("done");
        readButtons();
        if (upPressed && programNumber > 1) {
          programNumber -= 1;
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
        }
        if (downPressed) {
          programNumber += 1;
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
        }
        if (selectPressed) {
          programNumber = preferences.putInt("programNumber", programNumber);  // save it in EEPROM
          programNumber = preferences.getInt("programNumber", 1);              // retrieve from EEPROM
          screen = "settings";
          settingsSel = 3;
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
        }
      }

      // Action screen
      if (screen == "settings_action" && segNum == 0) {
        // action = 0 means X, action = 1 means Y
        actionScreen(actionSel);
        readButtons();

        if (upPressed && actionSel != 1) actionSel -= 1;
        if (downPressed && actionSel != 3) actionSel += 1;
        if (selectPressed) {
          if (actionSel == 1) action = 0;  // action = X
          if (actionSel == 2) action = 1;  // action = Y
          if (actionSel == 3) {
            preferences.putInt("action", action);
            tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
            screen = "settings";
            settingsSel = 3;
          }
        }
      }
    }

    //******************************
    // Running the firing program
    if (segNum >= 1) {

      readButtons();
      runningScreen();

      // Up arrow button
      if (upPressed) {
        if (screenNum == 1) {
          segNum = segQuantity + 2;
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
        }
        if (screenNum == 2 || (screenNum == 3 && optionNum == 1)) {
          screenNum = screenNum - 1;
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
        } else if (screenNum == 3 && optionNum >= 2) {
          optionNum = optionNum - 1;
        }
      }
      // Down arrow button
      if (downPressed) {
        if (screenNum <= 2) {
          screenNum = screenNum + 1;
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
        } else if (screenNum == 3 && optionNum <= 3) {
          optionNum = optionNum + 1;
        }
      }
      // Select / Start button
      if (selectPressed && screenNum == 3) {
        if (optionNum == 1) {  // Add 5 min
          segHold[segNum - 1] = segHold[segNum - 1] + 5;
          optionNum = 1;
          screenNum = 2;
        }

        if (optionNum == 2) {  // Add 5 deg
          segTemp[segNum - 1] = segTemp[segNum - 1] + 5;
          optionNum = 1;
          screenNum = 1;
        }

        if (optionNum == 3) {  // Goto next segment
          segNum = segNum + 1;
          optionNum = 1;
          screenNum = 2;
        }

        if (optionNum == 4) {  // SP = PV + required PID adjustment
          Serial.printf("rampStart original = %d ms \n", rampStart);
          calcSetPoint = pidInput;  // SP = PV
          Serial.printf("SP = %.2f = pidInput = %.2f \n", calcSetPoint, pidInput);
          double adjRampHours = (segTemp[segNum - 1] - pidInput) / segRamp[segNum - 1];  // "artificial" spanned t
          rampStart = millis() - (adjRampHours * 3600000.0);                             // "artificial" ramp start time
          Serial.printf("rampStart new = %d ms \n", rampStart);

          double TErampHours = (millis() - rampStart) / 3600000.0;
          double TEcalcSetPoint = lastTemp + (segRamp[segNum - 1] * TErampHours);
          Serial.printf("calculated SV = %.2f \n", TEcalcSetPoint);

          optionNum = 1;
          screenNum = 1;
        }

        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
      }

      // Update PID's / turn on heaters / update segment info
      checkDoor();
      updatePIDs();
      htrControl();
      updateSeg();
    }
  }
}

//******************************************************************************************************************************
//  HTRCONTROL: TURN HEATERS ON OR OFF
//******************************************************************************************************************************
void htrControl() {
  if (millis() - heatStart >= heatingCycle) {
    heatStart = millis();
  }  // maybe heat cycle should also call "updatePIDs" in running task

  if (pidOutput * heatingCycle / 100 >= millis() - heatStart && doorClosed) {  // door must be closed
    digitalWrite(heaterPin, HIGH);
  } else {
    digitalWrite(heaterPin, LOW);
  }
}
//******************************************************************************************************************************
//  SHUTDOWN: SHUT DOWN SYSTEM
//******************************************************************************************************************************
void shutDown() {
  // Turn off heating element relay
  digitalWrite(heaterPin, LOW);
  // Turn off PID algorithm
  setupPIDs(LOW);
}
//******************************************************************************************************************************
//  CHECKDOORISR: CHECK DOOR
//******************************************************************************************************************************
void checkDoor() {
  doorClosed_before = doorClosed;            // save previous door state
  if (digitalRead(limitSwitchPin) == LOW) {  // check new door state
    doorClosed = true;                       // door closed
    // Serial.println("door closed");
  } else doorClosed = false;                 // door open

  // Resume time measurements in PID algorithm
  if (!doorClosed_before && doorClosed && segNum >= 1) {  // only if door was opened and now it's closed (and firing)
    if (!isOnHold) {
      rampStart = millis() - (lastRampHours * 3600000);
      double newRampHours = (millis() - rampStart) / 3600000.0;
      // Serial.printf("new rampHours: %.6f \n", newRampHours);
    } else {
      holdStart = millis() - (lastHoldMins * 60000);
      holdMins = (millis() - holdStart) / 60000.0;
      // Serial.printf("holdMins: %.0f \n", holdMins);
    }
  }
}
//******************************************************************************************************************************
//  UPDATEPIDS: UPDATE THE PID LOOPS
//******************************************************************************************************************************
void updatePIDs() {
  // If door is open, exit and save ramp time (hold time saved in updateSeg)
  if (!doorClosed) {
    // If before it was closed, pause ramp time measurement (when ramping)
    if (doorClosed_before && !isOnHold) {
      lastRampHours = (millis() - rampStart) / 3600000.0;
      // Serial.printf("lastRampHours saved. last rampHours: %.6f \n", lastRampHours);
    }
    return;
  }

  // Get the last target temperature
  if (segNum != 1) lastTemp = segTemp[segNum - 2];
  // Calculate the new setpoint value.  Don't set above / below target temp
  if (isOnHold == false) {
    // Ramp: measure spanned t and calculate the SP with it
    rampHours = (millis() - rampStart) / 3600000.0;
    calcSetPoint = lastTemp + (segRamp[segNum - 1] * rampHours);
    // fix SP to target temp in case it's more than target temp
    if (segRamp[segNum - 1] >= 0 && calcSetPoint >= segTemp[segNum - 1]) {
      calcSetPoint = segTemp[segNum - 1];
    }
    if (segRamp[segNum - 1] < 0 && calcSetPoint <= segTemp[segNum - 1]) {
      calcSetPoint = segTemp[segNum - 1];
    }
  } else {
    calcSetPoint = segTemp[segNum - 1];  // Hold
  }
  // Set the target temp.
  pidSetPoint = calcSetPoint;
  // Update the PID controller based on new variables
  pidCont.Compute();  // (internally it will only compute if sample time has been elapsed)

  /* Use Mutex to save global variables*/
  xSemaphoreTake(DataSemaphore, portMAX_DELAY);  // Wait for the semaphore to become available
  calcSetPoint_global = pidSetPoint;
  pidOutput_global = pidOutput;
  xSemaphoreGive(DataSemaphore);  // Release the semaphore
}
//******************************************************************************************************************************
//  UPDATESEG: UPDATE THE PHASE AND SEGMENT
//******************************************************************************************************************************
void updateSeg() {
  // If door is closed, start hold phase or move to next segment (do I really want this??? what if ramp is negative, opening = cooling)
  if (doorClosed) {
    // Start the hold phase if temp is in range
    if ((!isOnHold && segRamp[segNum - 1] < 0 && pidInput <= (segTemp[segNum - 1] + tempRange)) ||   // if ramp is negative
        (!isOnHold && segRamp[segNum - 1] >= 0 && pidInput >= (segTemp[segNum - 1] - tempRange))) {  // if ramp is positive
      isOnHold = true;
      holdStart = millis();
    }
    // Go to the next segment if holding and hold time is completed
    if (isOnHold) {
      holdMins = (millis() - holdStart) / 60000.0;
      if (holdMins >= segHold[segNum - 1]) {
        segNum += 1;
        isOnHold = false;
        rampStart = millis();
        /* Use Mutex to save global variables*/
        xSemaphoreTake(DataSemaphore, portMAX_DELAY);  // Wait for the semaphore to become available
        segNum_global = segNum;
        xSemaphoreGive(DataSemaphore);  // Release the semaphore
      }
    }
  }
  // If door has just been opened
  if (!doorClosed && doorClosed_before) {
    lastHoldMins = (millis() - holdStart) / 60000.0;
    // Serial.printf("lastHoldMins saved. last holdMins: %.0f \n", lastHoldMins);
  }

  // Check if complete / turn off all zones
  if (segNum - 1 > segQuantity) {
    shutDown();
    segNum = 0;
    screen = "intro";
  }
}
//******************************************************************************************************************************
//  SETUPPIDS: INITIALIZE THE PID LOOPS
//******************************************************************************************************************************
void setupPIDs(int state) {
  pidCont.SetSampleTime(heatingCycle);  // so that the pid update time is not less than heating cycle
  pidCont.SetOutputLimits(0, 100);      // from 0%-100%
  pidOutput = 0;                        // output should start at 0% at new firings
  pidSetPoint = 0;                      // same for the setpoint
  if (state == HIGH) pidCont.SetMode(AUTOMATIC);
  if (state == LOW) pidCont.SetMode(MANUAL);
}
//******************************************************************************************************************************
//  READTEMPS: Read the temperatures
//******************************************************************************************************************************
void readTemps() {
  float t;

  while (1) {
    t = thermocouple.readThermocoupleTemperature();
    uint8_t fault = thermocouple.readFault();
    // if there's an error
    if (isnan(t) || (fault & MAX31856_FAULT_OPEN)) {
      displayErrorMessage("Thermocouple Error", "Failed to read TC", "System was shut down");
      shutDown();

      if (digitalRead(rstPin) == LOW) {
        esp_restart();
      }

      delay(250);
    } else {
      break;  // Exit loop if temperature read successfully
    }
  }

  // float t;
  // t = thermocouple.readThermocoupleTemperature();
  // if (tempScale == 'F') {
  //   t = 9 / 5 * t + 32;
  // }

  // // if there's an error
  // uint8_t fault = thermocouple.readFault();
  // while (isnan(t) || fault & MAX31856_FAULT_OPEN) {
  //   displayErrorMessage("Thermocouple Error", "Failed to read TC", "System was shut down");
  //   shutDown();
  //   delay(2500);
  // }
  
  // filter nonsense
  if (t < 5000 && t > 0) {
    if (tempScale == 'F') {
      t = 9 / 5 * t + 32;
    }
    pidInput_global = t + tempOffset;  // add any offset
    /* Use Mutex to save local pidInput */
    xSemaphoreTake(DataSemaphore, portMAX_DELAY);  // Wait for the semaphore to become available
    pidInput = pidInput_global;
    xSemaphoreGive(DataSemaphore);  // Release the semaphore
  }
}
//******************************************************************************************************************************
//  runningScreen: TFT SCREEN WHEN RUNNING
//******************************************************************************************************************************
void runningScreen() {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  // Operator screen (temperature)
  if (screenNum == 1) {
    tft.setCursor(10, 30);
    tft.print(F("pv"));
    tft.setCursor(10, 140);
    tft.print(F("sv"));
    tft.setCursor(60, 160);
    tft.setTextSize(8);
    tft.printf("%04d%c", (int)pidSetPoint, tempScale);
    tft.setCursor(60, 60);
    tft.setTextSize(8);
    tft.printf("%04d%c", (int)pidInput, tempScale);
  }
  // Info screen
  if (screenNum == 2) {
    tft.setCursor(0, 40);
    tft.printf("PROGRAM %i: \n\n%s", programNumber, programDesc1);
    tft.setCursor(0, 120);
    tft.printf("SEGMENT: %i/%i", segNum, segQuantity);
    if (isOnHold == 0) {
      tft.setCursor(160, 150);
      tft.printf("Ramp to %i%c", segTemp[segNum - 1], tempScale);
      tft.setCursor(160, 170);
      tft.printf("at %i%c/hr", segRamp[segNum - 1], tempScale);
    } else {
      tft.setCursor(160, 150);
      tft.printf("Hold at %i%c \n", segTemp[segNum - 1], tempScale);
      tft.setCursor(160, 170);
      tft.printf("for %.0f / %i min", holdMins, segHold[segNum - 1]);
    }
  }
  // Tools screen
  if (screenNum == 3) {
    tftPrintCenterWidth("TOOLS:", 30);
    char* option1 = "  Add 5 min  ";
    char* option2 = "  Increase 5 deg  ";
    char* option3 = "  Skip to next segment  ";
    char* option4 = "  Equal SV and PV  ";
    switch (optionNum) {
      case 1:
        option1 = "> Add 5 min <";
        break;
      case 2:
        option2 = "> Increase 5 deg <";
        break;
      case 3:
        option3 = "> Skip to next segment <";
        break;
      case 4:
        option4 = "> Equal SV and PV <";
        break;
    }
    tftPrintCenterWidth(option1, 80);
    tftPrintCenterWidth(option2, 100);
    tftPrintCenterWidth(option3, 120);
    tftPrintCenterWidth(option4, 140);
  }
}
//******************************************************************************************************************************
//  SETTINGSSCREEN: UPDATE TFT WITH SETTINGS MENU
//******************************************************************************************************************************
void settingsScreen(int sel) {
  char* option1 = "  SELECT PROGRAM  ";
  char* option2 = "  ACTION  ";
  char* option3 = "  DONE  ";

  switch (sel) {
    case 1:
      option1 = "> SELECT PROGRAM <";
      break;
    case 2:
      option2 = "> ACTION <";
      break;
    case 3:
      option3 = "> DONE <";
      break;
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tftPrintCenterWidth("SETTINGS", 40);
  tft.setTextSize(2);
  tftPrintCenterWidth(option1, 120);
  tftPrintCenterWidth(option2, 150);
  tft.setCursor(220, 200);
  tft.print(F(option3));
}
//******************************************************************************************************************************
//  INTROSCREEN: UPDATE TFT WITH INTRO MENU
//******************************************************************************************************************************
void introScreen(int sel) {
  tft.setCursor(10, 70);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.print(F("pv"));
  tft.setCursor(60, 60);
  tft.setTextSize(8);
  tft.printf("%04d%c", (int)pidInput, tempScale);
  tft.setTextSize(3);

  if (sel == 1) {
    tftPrintCenterWidth("> START <", 140);
    tftPrintCenterWidth("  SETTINGS  ", 180);
  }
  if (sel == 2) {
    tftPrintCenterWidth("  START  ", 140);
    tftPrintCenterWidth("> SETTINGS <", 180);
  }
}
//******************************************************************************************************************************
//  actionScreen: DISPLAYS ACTION MODE
//******************************************************************************************************************************
void actionScreen(int actionSel) {
  char* text1 = "  X  ";
  char* text2 = "  Y  ";
  char* text3 = "  DONE  ";

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tftPrint("ACTION", 2, 40);

  switch (actionSel) {
    case 1:
      text1 = "> X <";
      break;
    case 2:
      text2 = "> Y <";
      break;
    case 3:
      text3 = "> DONE <";
      break;
  }

  if (action == 0) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tftPrintCenterWidth(text1, 100);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tftPrintCenterWidth(text2, 130);
  }
  if (action == 1) {
    tftPrintCenterWidth(text1, 100);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tftPrintCenterWidth(text2, 130);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  tftPrint(text3, 220, 200);
}
//******************************************************************************************************************************
//  openProgram: OPEN AND LOAD A FIRING PROGRAM FILE / DISPLAY ON SCREEN
//******************************************************************************************************************************
void openProgram() {

  // Setup all variables
  int col = 1;            // Column number (of text file).  First column is one.
  int row = 1;            // Row number (of text file).  First row is one.
  char tempChar;          // Temporary character holder (read one at a time from file)
  char tempLine[21];      // Temporary character array holder
  int tempLoc = 0;        // Current location of next character to place in tempLine array
  char programDesc2[21];  // Program description #2 (second line of text file)
  char programDesc3[21];  // Program description #3 (third line of text file)

  // Clear the arrays
  memset(programDesc1, 0, sizeof(programDesc1));
  memset(segRamp, 0, sizeof(segRamp));
  memset(segTemp, 0, sizeof(segTemp));
  memset(segHold, 0, sizeof(segHold));

  // Make sure you can open the file
  sprintf(tempLine, "/%d.txt", programNumber);
  File myFile = SD.open(tempLine, FILE_READ);

  if (myFile == false) {
    tft.setCursor(20, 40);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.print(F("SELECT PROGRAM: "));
    tft.println(programNumber);
    tft.setCursor(20, 60);
    tft.print(F("Can't find/open file"));
    programOK = false;
    return;
  }
  // Load the data
  while (myFile.available() > 0) {

    // Read a single character
    tempChar = myFile.read();

    if (tempChar == 13) {  // Carriage return: Read another char (it is always a line feed / 10).  Add null to end.
      myFile.read();
      tempLine[tempLoc] = '\0';
    } else if (tempChar == 44) {  // Comma: Add null to end.
      tempLine[tempLoc] = '\0';
    } else if (tempLoc <= 19) {  // Add it to the temp line array
      tempLine[tempLoc] = tempChar;
      tempLoc = tempLoc + 1;
    }

    if (row == 1 && tempChar == 13) {
      memcpy(programDesc1, tempLine, 21);
    } else if (row == 2 && tempChar == 13) {
      memcpy(programDesc2, tempLine, 21);
    } else if (row == 3 && tempChar == 13) {
      memcpy(programDesc3, tempLine, 21);
    } else if (row >= 4 && col == 1 && tempChar == 44) {
      segRamp[row - 4] = atoi(tempLine);
    } else if (row >= 4 && col == 2 && tempChar == 44) {
      segTemp[row - 4] = atoi(tempLine);
    } else if ((row >= 4 && col == 3 && tempChar == 13) || myFile.available() == 0) {
      segHold[row - 4] = atoi(tempLine);
    }

    if (tempChar == 13) {  // End of line.  Reset everything and goto next line
      memset(tempLine, 0, 21);
      tempLoc = 0;
      row = row + 1;
      col = 1;
    }

    if (tempChar == 44) {  // Comma.  Reset everything and goto 1st column
      memset(tempLine, 0, 21);
      tempLoc = 0;
      col = col + 1;
    }

  }  // end of while(myFile.available ...

  // Close the file
  myFile.close();

  // Set some variables
  segQuantity = row - 3;
  programOK = true;

  // Fix Ramp values so it will show the correct sign (+/-).  This will help to determine when to start hold.
  for (int i = 0; i < segQuantity; i++) {
    segRamp[i] = abs(segRamp[i]);
    if (i >= 1) {
      if (segTemp[i] < segTemp[i - 1]) {
        segRamp[i] = -segRamp[i];
      }
    }
  }

  // Display on the screen
  tft.setCursor(20, 40);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.printf("SELECT PROGRAM: %i \n\n %s \n\n %s \n\n %s", programNumber, programDesc1, programDesc2, programDesc3);
}
//******************************************************************************************************************************
//  DISPLAYERRORMESSAGE: PRINT AN ERROR ON TFT
//******************************************************************************************************************************
void displayErrorMessage(char* title, char* message1, char* message2) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(4);
  tftPrintCenterWidth("ERROR", 80);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tftPrintCenterWidth(title, 150);
  tftPrintCenterWidth(message1, 180);
  tftPrintCenterWidth(message2, 210);
}
//******************************************************************************************************************************
//  INITWIFI: INITIALIZE WIFI
//******************************************************************************************************************************
void initWiFi() {
  // WiFi initialization
  WiFi.mode(WIFI_STA);
  for (int i = 0; i < sizeof(network) / sizeof(network[0]); i++) {
    wifiMulti.addAP(network[i], password[i]);
  }

  tft.setTextColor(TFT_WHITE, bar_color);
  tft.setTextSize(1);
  tft.fillRect(0, 0, 320, 20, bar_color);  // clear top notch
  tft.drawString("Connecting...", 240, 8, 1);

  // try connecting for 10s maximum
  unsigned long init_millis = millis();
  while (millis() - init_millis < 10000) {
    if (wifiMulti.run() != WL_CONNECTED) {
      delay(300);
      Serial.print(".");
    } else {
      Serial.println("WiFi connected");
      Serial.println("IP address: ");  // Print local IP address
      Serial.println(WiFi.localIP());
      Serial.print("Connected to:\t");
      Serial.println(WiFi.SSID());
      connection_OK = true;
      break;
    }
  }
}
//******************************************************************************************************************************
//  DRAWTOPBAR: DRAW WIFI BARS, PERCENTAGE AND INFLUX STATE
//******************************************************************************************************************************
void drawTopBar() {
  int centerY = 10;
  tft.fillRect(0, 0, 320, 20, bar_color);  // clear top notch
  tft.setTextSize(1);

  bool published;
  bool connected;
  xSemaphoreTake(DataSemaphore, portMAX_DELAY);
  published = influx_OK;
  connected = connection_OK;
  xSemaphoreGive(DataSemaphore);

  if (connected) {
    int8_t quality = getWifiQuality();
    tft.setTextColor(TFT_WHITE, bar_color);
    tft.drawString(String(quality) + "%", 290, centerY, 1);
    for (int8_t i = 0; i < 4; i++) {
      for (int8_t j = 0; j < 2 * (i + 1); j++) {
        if (quality > i * 25) {
          tft.drawPixel(270 + 2 * i, (centerY + 7) - j, TFT_GREEN);
        }
      }
    }
  } else {
    tft.setTextColor(TFT_RED, bar_color);
    tft.drawString("OFFLINE", 270, centerY, 1);
  }
  // Draw Influx check mark or cross
  if (published) {
    tft.drawLine(16, centerY + 1, 19, centerY + 4, TFT_GREEN);  // Draw the first diagonal line
    tft.drawLine(19, centerY + 3, 23, centerY + 0, TFT_GREEN);  // Draw the second diagonal line
    tft.drawLine(16, centerY + 2, 19, centerY + 5, TFT_GREEN);  // Draw the first inner diagonal line
    tft.drawLine(19, centerY + 4, 23, centerY + 1, TFT_GREEN);  // Draw the second inner diagonal line
    tft.drawLine(17, centerY + 1, 20, centerY + 4, TFT_GREEN);  // Draw the first outer diagonal line
    tft.drawLine(20, centerY + 3, 24, centerY + 0, TFT_GREEN);  // Draw the second outer diagonal line
  } else {
    tft.drawLine(17, centerY - 1, 23, centerY + 5, TFT_RED);  // Draw the first diagonal line
    tft.drawLine(23, centerY - 1, 17, centerY + 5, TFT_RED);  // Draw the second diagonal line
  }
}
//******************************************************************************************************************************
//  GETWIFIQUALITY: GETS RSSI AND CONVERTS IT FROM DBM TO %
//******************************************************************************************************************************
int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) {
    return 0;
  } else if (dbm >= -50) {
    return 100;
  } else {
    return 2 * (dbm + 100);
  }
}
//******************************************************************************************************************************
//  RESETTFT: RESETS TFT WHEN CONTACTORS ARE OPENED (EMF) OR USER CALLS IT
//******************************************************************************************************************************
void resetTFT() {
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
}
//******************************************************************************************************************************
//  READBUTTONS: READ IF BUTTONS ARE PRESSED
//******************************************************************************************************************************
void readButtons() {
  upPressed = false;
  selectPressed = false;
  downPressed = false;

  if (digitalRead(upPin) == LOW) {
    upPressed = true;
    btnBounce(upPin);
    // Serial.println("up pressed");
  }
  if (digitalRead(selectPin) == LOW) {
    selectPressed = true;
    btnBounce(selectPin);
  }
  if (digitalRead(downPin) == LOW) {
    downPressed = true;
    btnBounce(downPin);
    // Serial.println("down pressed");
  }
}
//******************************************************************************************************************************
//  BTNBOUNCE: HOLD UNTIL BUTTON IS RELEASED.  DELAY FOR ANY BOUNCE
//******************************************************************************************************************************
void btnBounce(int btnPin) {
  while (digitalRead(btnPin) == LOW) {}
  delay(25);
}
//******************************************************************************************************************************
//  TFTPRINTCENTERWIDTH: CENTERS CURSOR ON WIDTH AND PRINTS
//******************************************************************************************************************************
void tftPrintCenterWidth(char* text, int y) {
  tft.setCursor((tftwidth - tft.textWidth(text)) / 2, y);
  tft.print(F(text));
}
//******************************************************************************************************************************
//  TFTPRINT: SETS CURSOR ON (X,Y) AND PRINTS
//******************************************************************************************************************************
void tftPrint(char* text, int x, int y) {
  tft.setCursor(x, y);
  tft.print(F(text));
}
