// Libraries to include
#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_MAX31856.h"  // thermocouple card library (56)
#include <PID_v1.h>             // PID temp control library
#include <SPI.h>                // Serial Peripheral Interface library
#include <FS.h>                 // File system
#include <SPIFFS.h>             // For SPI full file system
#include <Preferences.h>        // library to save variables to EPROOM
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <set>
#include <ArduinoJson.h>
#include "FiringProgram.h"

#include "gui.h"

// Setup user variables (CHANGE THESE IN HEADER FILE)
#include "userSetup.h"

// Include tasks
#include "database_task.h"

// Internal variables
unsigned long TFT_start;     // Exact time you refreshed the TFT screen (ms).  Based on millis().
unsigned long tempStart;     // Exact time you last updated the temperature (ms). Based on millis().
unsigned long WiFi_start;    // Exact time you refreshed the WiFi quality symbol (ms).
unsigned long Influx_start;  // Exact time you last uploaded data to Influx DB (ms).
unsigned long programStart;  // Exact time you started running the program (ms).  Based on millis().
unsigned long heatStart;     // Exact time you started the new heating cycle (ms).  Based on millis().
unsigned long rampStart;     // Exact time the ramp phase of the segment started (ms).  Based on millis().
unsigned long holdStart;     // Exact time the hold phase of the segment started (ms).  Based on millis().
unsigned long lastSSIDUpdate;
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
int segQuantity;             // Last segment number in firing program
int lastTemp;                // Last setpoint temperature (degrees)
int programNumber;           // Current firing program number.  This ties to the file name (ex: 1.txt, 2.txt).
int segNum = 0;              // Current segment number running in firing program.  0 means a program hasn't been selected yet.
int segNum_global = 0;       // "" but for both tasks
int action;                  // Action methods
bool programOK = false;      // Is the program you loaded OK?
bool isOnHold = 0;           // Current segment phase.  0 = ramp.  1 = hold.
bool doorClosed = true;      // Kiln door has a limit switch attached to it.
bool doorClosed_before;      // To check if door state just changed
bool influx_OK = false;      // Is InfluxDB publishing working
bool connection_OK = false;  // Is WiFi connection working
bool captive_mode = false;
bool receivedCredentials = false;
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";

FiringProgram currentProgram;
FiringProgram serverProgram;
String ssidList;
String ssid;
String password;

//******************************************************************************************************************************
//  SETUP: INITIAL SETUP (RUNS ONCE DURING START)
//******************************************************************************************************************************
/* Initialize stuff */
AsyncWebServer server(80);
DNSServer dnsServer;
PID pidCont = { PID(&pidInput, &pidOutput, &pidSetPoint, Kp, Ki, Kd, DIRECT) };
Preferences preferences;
Adafruit_MAX31856 thermocouple(thermocoupleCS, MAX_DI, MAX_DO, MAX_CLK);
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request) {
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html","text/html"); 
  }
};

// put function declarations here:
void main_task(void* parameter);
void htrControl();
void shutDown();
void checkDoor();
void updatePIDs();
void updateSeg();
void setupPIDs(int state);
void SPequalPV();
void readTemps();
void setSegNum(int value);
int getSegNum();
void setLastTemp();
void setHeatStart(unsigned long value);
void setRampStart(unsigned long value);
void setProgramStart(unsigned long value);
int setProgramNumber(int value);
int setAction(int value);
void handleCaptiveModeToggle();
void handleCaptiveMode();
bool getIsOnHold();
double getHoldMins();
void openProgram();
int8_t getWifiQuality();
String readFile(fs::FS& fs, const char* path);
void writeFile(fs::FS& fs, const char* path, const char* message);
void StartCaptivePortal();
void setupServer();
void getSSIDs();
void saveConfigFile();
int extractSegmentNumber(const String& paramName);

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
  pinMode(ledPin, OUTPUT);
  // doorClosed = !digitalRead(limitSwitchPin);  // door is closed when pin is LOW
  // attachInterrupt(digitalPinToInterrupt(limitSwitchPin), checkDoorISR, CHANGE);

  gui_start();

  // Setup thermocouple
  while (!thermocouple.begin()) {
    disp_error_msg("TC ERROR","Could not initialize thermocouple.", "Check connections");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }
  thermocouple.setThermocoupleType(TCTYPE);  // thermocouple.getThermocoupleType

  // Mount SPIFFS file system
  while (!SPIFFS.begin(true)) {
    disp_error_msg("SPIFFS Error", "Can't setup file system.", "Make sure files are uploaded.");
    if (digitalRead(rstPin) == LOW) esp_restart();
    delay(200);
  }

  // Retrieve data from SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  password = readFile(SPIFFS, passPath);
  Serial.println(ssid);
  Serial.println(password);

  // setup and retrieve data from EEPROM
  preferences.begin("my-app", false);
  programNumber = preferences.getInt("programNumber", 1);
  action = preferences.getInt("action", 0);  // 0 = X

  // Create the main task and set its affinity to core 1
  xTaskCreatePinnedToCore(main_task, "Main", 40960, NULL, 1, NULL, 1);
  // Create the publishing task and set its affinity to core 0
  xTaskCreatePinnedToCore(database_task, "Database", 40960, NULL, 1, NULL, 0);

}

void loop() {
  // Nothing to do here
}

//*******************************************************************************************************************************

// Task function to perform all Kiln control on core 1
void main_task(void* parameter) {
  while (1) {
    //******************************
    // Shutdown if too hot
    if (pidInput >= maxTemp) {
      disp_error_msg("MAX TEMP REACHED", "System was shut down.", "Press RESET to restart.");
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
      bool published, connected;
      xSemaphoreTake(mutex, portMAX_DELAY);
      published = influx_OK;
      connected = connection_OK;
      xSemaphoreGive(mutex);

      int8_t quality;
      if (connected) {
        quality = getWifiQuality();
      }

      drawTopBar(quality, published, connected);
      WiFi_start = millis();
    }

    // when not running
    if (segNum == 0) {
      gui_idle();
    }

    //******************************
    // Running the firing program
    if (segNum >= 1) {
      gui_firing();

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
  if (segNum != 1) lastTemp = currentProgram.segments[segNum - 2].targetTemperature;
  // Calculate the new setpoint value.  Don't set above / below target temp
  if (isOnHold == false) {
    // Ramp: measure spanned t and calculate the SP with it
    rampHours = (millis() - rampStart) / 3600000.0;
    calcSetPoint = lastTemp + (currentProgram.segments[segNum - 1].firingRate * rampHours);
    // fix SP to target temp in case it's more than target temp
    if (currentProgram.segments[segNum - 1].firingRate >= 0 && calcSetPoint >= currentProgram.segments[segNum - 1].targetTemperature) {
      calcSetPoint = currentProgram.segments[segNum - 1].targetTemperature;
    }
    if (currentProgram.segments[segNum - 1].firingRate < 0 && calcSetPoint <= currentProgram.segments[segNum - 1].targetTemperature) {
      calcSetPoint = currentProgram.segments[segNum - 1].targetTemperature;
    }
  } 
  else {
    calcSetPoint = currentProgram.segments[segNum - 1].targetTemperature;  // Hold
  }
  // Set the target temp.
  pidSetPoint = calcSetPoint;
  // Update the PID controller based on new variables
  pidCont.Compute();  // (internally it will only compute if sample time has been elapsed)

  /* Use Mutex to save global variables*/
  xSemaphoreTake(mutex, portMAX_DELAY);  // Wait for the semaphore to become available
  calcSetPoint_global = pidSetPoint;
  pidOutput_global = pidOutput;
  xSemaphoreGive(mutex);  // Release the semaphore
}
//******************************************************************************************************************************
//  UPDATESEG: UPDATE THE PHASE AND SEGMENT
//******************************************************************************************************************************
void updateSeg() {
  // If door is closed, start hold phase or move to next segment (do I really want this??? what if ramp is negative, opening = cooling)
  if (doorClosed) {
    // Start the hold phase if temp is in range
    if ((!isOnHold && currentProgram.segments[segNum - 1].firingRate < 0 && pidInput <= (currentProgram.segments[segNum - 1].targetTemperature + tempRange)) ||   // if ramp is negative
        (!isOnHold && currentProgram.segments[segNum - 1].firingRate >= 0 && pidInput >= (currentProgram.segments[segNum - 1].targetTemperature - tempRange))) {  // if ramp is positive
      isOnHold = true;
      holdStart = millis();
    }
    // Go to the next segment if holding and hold time is completed
    if (isOnHold) {
      holdMins = (millis() - holdStart) / 60000.0;
      if (holdMins >= currentProgram.segments[segNum-1].holdingTime) {
        segNum += 1;
        isOnHold = false;
        rampStart = millis();
        /* Use Mutex to save global variables*/
        xSemaphoreTake(mutex, portMAX_DELAY);  // Wait for the semaphore to become available
        segNum_global = segNum;
        xSemaphoreGive(mutex);  // Release the semaphore
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
    goToIntroScreen();
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
// SP is set equal to PV, times are adjusted acordingly
void SPequalPV() {
  Serial.printf("rampStart original = %d ms \n", rampStart);
  calcSetPoint = pidInput;  // SP = PV
  Serial.printf("SP = %.2f = pidInput = %.2f \n", calcSetPoint, pidInput);
  int segTemp = currentProgram.segments[segNum - 1].targetTemperature;
  int segRamp = currentProgram.segments[segNum - 1].firingRate;
  double adjRampHours = (segTemp - pidInput) / segRamp;  // "artificial" spanned t
  rampStart = millis() - (adjRampHours * 3600000.0);    // "artificial" ramp start time
  Serial.printf("rampStart new = %d ms \n", rampStart);

  double TErampHours = (millis() - rampStart) / 3600000.0;
  double TEcalcSetPoint = lastTemp + (segRamp * TErampHours);
  Serial.printf("calculated SP = %.2f \n", TEcalcSetPoint);
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
      disp_error_msg("Thermocouple Error", "Failed to read TC", "System was shut down");
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
  //   disp_error_msg("Thermocouple Error", "Failed to read TC", "System was shut down");
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
    xSemaphoreTake(mutex, portMAX_DELAY);  // Wait for the semaphore to become available
    pidInput = pidInput_global;
    xSemaphoreGive(mutex);  // Release the semaphore
  }
}
//******************************************************************************************************************************
//  openProgram: OPEN AND LOAD A FIRING PROGRAM FILE / DISPLAY ON SCREEN
//******************************************************************************************************************************
void openProgram() {
  // Setup all variables
  StaticJsonDocument<2048> json;

  // Make sure you can open the file
  char filename[20];
  sprintf(filename, "/firingProgram_%d.json", programNumber);
  fs::File myFile = SPIFFS.open(filename, FILE_READ);
  // Parse the JSON file
  DeserializationError error = deserializeJson(json, myFile);

  if (!myFile || error) {
    disp_program_error();
    programOK = false;
    return;
  }

  // Load the data into currentProgram struct
  currentProgram.name = json["name"].as<String>();
  currentProgram.duration = json["duration"].as<String>();
  currentProgram.createdDate = json["createdDate"].as<String>();

  JsonArray segmentsArray = json["segments"].as<JsonArray>();
  segQuantity = min(segmentsArray.size(), static_cast<size_t>(MAX_SEGMENTS));  // MAX_SEGMENTS is the max size of your arrays

  for (int i = 0; i < segQuantity; i++) {
    JsonObject segment = segmentsArray[i];
    currentProgram.segments[i].firingRate = segment["firingRate"].as<int>();
    currentProgram.segments[i].targetTemperature = segment["targetTemperature"].as<int>();
    currentProgram.segments[i].holdingTime = segment["holdingTime"].as<int>();

    // Fix Ramp values to show the correct sign
    currentProgram.segments[i].firingRate = abs(currentProgram.segments[i].firingRate);
    if (i >= 1) {
      if (currentProgram.segments[i].targetTemperature < currentProgram.segments[i - 1].targetTemperature) {
        currentProgram.segments[i].firingRate = -currentProgram.segments[i].firingRate;
      }
    }
  }

  programOK = true;
  myFile.close();

  // Display on the screen
  disp_program();

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

//*******************************************************************************************************************************
// GUI called functions
//******************************************************************************************************************************

void setSegNum(int value) {
    segNum = value;
}

int getSegNum() {
  return segNum;
}

void setLastTemp() {
  lastTemp = pidInput;
}

void setHeatStart(unsigned long value) {
  heatStart = value;
}

void setRampStart(unsigned long value) {
  rampStart = value;
}

void setProgramStart(unsigned long value) {
  programStart = value;
}

int setProgramNumber(int value) {
  preferences.putInt("programNumber", programNumber);  // save it in EEPROM
  return preferences.getInt("programNumber", 1);    // read it back from EEPROM
}

int setAction(int value){
  preferences.putInt("action", action);  // save it in EEPROM
  return preferences.getInt("action", 0);    // read it back from EEPROM
}

void handleCaptiveModeToggle() {
    captive_mode = !captive_mode;
    if (captive_mode) {
        StartCaptivePortal();
        lastSSIDUpdate = millis() - 15000;
    } 
    else {
      server.end();
    }
}

void handleCaptiveMode() {
  dnsServer.processNextRequest();
  delay(10);
  if (millis() - lastSSIDUpdate > 15000) {
    getSSIDs();
    lastSSIDUpdate = millis();
  }
  if (receivedCredentials) {
    Serial.println("Credentials changed. Initializing WiFi again.");
    receivedCredentials = false;
    captive_mode = false;
    server.end();
    // initWiFi();
  }
}

bool getIsOnHold() {
  return isOnHold;
}

double getHoldMins() {
  return holdMins;
}

//******************************************************************************************************************************
// Server related functions
//******************************************************************************************************************************
void getSSIDs() {
  // Provide JSON response with dynamic values
  int networks = WiFi.scanNetworks();
  std::set<String> uniqueSSIDs;

  // Get the first 5 unique SSIDs
  for (int i = 0; i < min(5, networks); ++i) {
    String ssid_i = WiFi.SSID(i);
    // Check if the SSID is not repeated
    if (uniqueSSIDs.find(ssid_i) == uniqueSSIDs.end()) {
      uniqueSSIDs.insert(ssid_i);
    }
  }

  // Construct JSON array with empty values
  ssidList = "{";
  for (int i = 1; i <= 5; i++) {
    String ssidProperty = "SSID" + String(i);
    if (i > 1) {
      ssidList += ", ";
    }
    ssidList += "\"" + ssidProperty + "\":\"";
    if (i <= uniqueSSIDs.size()) {
      ssidList += *std::next(uniqueSSIDs.begin(), i - 1);
    }
    ssidList += "\"";
  }
  ssidList += "}";

  // Serial.printf("Updated SSID list:\n %s \n ", ssidList.c_str());
}

void StartCaptivePortal() {
  Serial.println("Setting up AP Mode");

  WiFi.mode(WIFI_AP);
  WiFi.softAP("The Kiln Controller", NULL);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  Serial.println("Setting up Async WebServer");
  setupServer();

  Serial.println("Starting DNS Server");
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  server.begin();

  Serial.println("Done!");
}

void setupServer() {
  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/index.html", "text/html");
    Serial.println("Client connected");
  });

  // Route for WiFi manager config page
  server.on("/wifi-manager", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/wifimanager.html", "text/html");
    Serial.println("WiFi manager initiated");
  });

  // Route for the program editor page
  server.on("/program-editor", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/firingProgram.html", "text/html");
  });

  // Route for the LED control page
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SPIFFS, "/control.html", "text/html");
  });

  // Retrieving available SSIDs
  server.on("/getSSIDList", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("getting SSIDs");
    request->send(200, "application/json", ssidList); // sent as a JSON array
  });

  // Retrieving WiFi credentials
  server.on("/wifi-manager", HTTP_POST, [](AsyncWebServerRequest* request) {
    int params = request->params();
    for (int i = 0; i < params; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->isPost()) {
        // HTTP POST ssid value
        if (p->name() == "ssid") {
          ssid = p->value().c_str();
          Serial.printf("Received SSID: %s\n", ssid);
          // Write file to save value
          writeFile(SPIFFS, ssidPath, ssid.c_str());
        }
        // HTTP POST pass value
        if (p->name() == "password") {
          password = p->value().c_str();
          Serial.printf("Received Password: %s\n", password);
          // Write file to save value
          writeFile(SPIFFS, passPath, password.c_str());
        }
        receivedCredentials = true;
      }
    }
    request->send(200, "text/html", "Values have been saved");
    delay(3000);
  });

  // Retrieving firing schedule files
  server.on("/program-editor", HTTP_POST, [](AsyncWebServerRequest* request) {
    Serial.println("program editor posted");
    int params = request->params();
    int maxIndex = 0;

    for (int i = 0; i < params; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->isPost()) {
        // Process program defining parameters
        if (p->name() == "programNumber") {
          serverProgram.programNumber = p->value().toInt();
          Serial.printf("Received Program Number: %d\n", serverProgram.programNumber);
        }
        if (p->name() == "createdDate") {
          serverProgram.createdDate = p->value().c_str();
          // Serial.printf("Created Date: %s\n", serverProgram.createdDate.c_str());
        }
        if (p->name() == "name") {
          serverProgram.name = p->value().c_str();
          Serial.printf("Program Name: %s\n", serverProgram.name.c_str());
        }
        if (p->name() == "duration") {
          serverProgram.duration = p->value().c_str();
          // Serial.printf("Duration: %s\n", serverProgram.duration.c_str());
        }

        // Now process segment specific parameters
        if (p->name().startsWith("target") || p->name().startsWith("speed") || p->name().startsWith("hold")) {
          int segmentIndex = extractSegmentNumber(p->name()) - 1; // Convert to 0-based index
          maxIndex = max(segmentIndex, maxIndex);

          if (p->name().startsWith("target")) {
            serverProgram.segments[segmentIndex].targetTemperature = p->value().toInt();
          } else if (p->name().startsWith("speed")) {
            serverProgram.segments[segmentIndex].firingRate = p->value().toInt();
          } else if (p->name().startsWith("hold")) {
            serverProgram.segments[segmentIndex].holdingTime = p->value().toInt();
          }
        }
      }
    }
    serverProgram.segmentQuantity = maxIndex + 1;
    Serial.printf("Segment quantity: %d\n", serverProgram.segmentQuantity);

    saveConfigFile();

    request->send(200, "text/html", "Values have been saved");
    delay(3000);
  });

  // Route to set GPIO state to HIGH
  server.on("/control/on", HTTP_POST, [](AsyncWebServerRequest* request) {
    digitalWrite(ledPin, HIGH);
    request->send(SPIFFS, "/control.html", "text/html");
  });

  // Route to set GPIO state to LOW
  server.on("/control/off", HTTP_POST, [](AsyncWebServerRequest* request) {
    digitalWrite(ledPin, LOW);
    request->send(SPIFFS, "/control.html", "text/html");
  });  
  
  // Redirect any request to the root to the configuration page (catch all route)
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->redirect("/");
  });

  server.serveStatic("/", SPIFFS, "/");
}

int extractSegmentNumber(const String& paramName) {
    // Find the position of the first digit in the parameter name
    for (unsigned int i = 0; i < paramName.length(); i++) {
        if (isDigit(paramName.charAt(i))) {
            // Extract the substring from the first digit to the end of the string
            String numberStr = paramName.substring(i);
            return numberStr.toInt(); // Convert the number string to an integer
        }
    }
    return -1; // Return -1 if no digit is found
}

void saveConfigFile() {
  Serial.println(F("Saving config"));
  StaticJsonDocument<2048> json;
  json["name"] = serverProgram.name;
  json["duration"] = serverProgram.duration;
  json["createdDate"] = serverProgram.createdDate;

  JsonArray segmentsArray = json.createNestedArray("segments");

  for (int i = 0; i < serverProgram.segmentQuantity; i++) {
    JsonObject segment = segmentsArray.createNestedObject();
    segment["targetTemperature"] = serverProgram.segments[i].targetTemperature;
    segment["firingRate"] = serverProgram.segments[i].firingRate;
    segment["holdingTime"] = serverProgram.segments[i].holdingTime;
  }

  String fileName = "/firingProgram_" + String(serverProgram.programNumber) + ".json";
  fs::File configFile = SPIFFS.open(fileName, "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  Serial.println();
  configFile.close();
}

// Write file to SPIFFS
void writeFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);

  fs::File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// Read File from SPIFFS
String readFile(fs::FS& fs, const char* path) {
  Serial.printf("Reading file: %s\r\n", path);

  fs::File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}
