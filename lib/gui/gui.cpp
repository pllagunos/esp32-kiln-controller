#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>

#include "userSetup.h"
#include "common.h"

// fix the include errors from network, but WHY
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFiMulti.h>
#include <FS.h>          
#include <SPIFFS.h>      
#include <set>
#include <PID_v1.h>
#include "network.h" // This causes circular dependency

#include "heat_control.h"
#include "gui.h"

extern heat_control controller;
extern Network network;

static const char *TAG = "gui";

// local variables
namespace {
  char *screen = "intro";      // Variable that holds screen type (start with intro)
  bool upPressed = false;      // Up button press state
  bool selectPressed = false;  // Select button press state
  bool downPressed = false;    // Down button press state
  bool programOK;              // Is the program loaded correctly?
  bool fileExists = true;      // Assume file exists initially 

  int introSel = 1;            // Intro menu selected option (start or settings)
  int confirmSel;              // Confirm selected option (back or OK)
  int settingsSel = 1;         // Settings menu selected setting
  int actionSel = 1;           // Option selected from action screen
  int configSel = 1;
  int screenNum = 1;           // Screen number displayed during firing (1 = temps / 2 = program info / 3 = tools / 4 = done
  int optionNum = 1;           // Option selected from screen #3
  int programNumber;           // Current firing program number.  This ties to the file name
  int action;                  // Action methods (what for?)
  
  unsigned long topBar_start;  // Time to start top bar refresh
}

// global variables
// extern int action;
// extern bool captive_mode;
// extern bool receivedCredentials;

Preferences preferences; // For saving settings
TFT_eSPI tft = TFT_eSPI();

void gui_start() {
  pinMode(upPin, INPUT_PULLUP);
  pinMode(downPin, INPUT_PULLUP);
  pinMode(selectPin, INPUT_PULLUP);
  pinMode(rstPin, INPUT_PULLUP);
  
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  // setup and retrieve data from EEPROM
  preferences.begin("my-app", false);
  programNumber = preferences.getInt("programNumber", 1);
  action = preferences.getInt("action", 0); // 0 = X
}

void gui_run() {

  disp_top_bar();

  if (controller.getSegNum() == 0) gui_idle();

  if (controller.getSegNum() >= 1) gui_firing();
  
}

void gui_idle() {
  
  if (screen == "intro") {
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
      screen = "settings"; // user pressed settings
      settingsSel = 1;
      tft.fillRect(0, 20, 320, 240 - 20,
                   TFT_BLACK); // clear screen except top notch
    }

    /* User pressed START */
    if (selectPressed && introSel == 1) {
      // shift_register.set(gasPin, HIGH);  // allow gas contactor to be
      // manually energized
      screen = "confirm"; // go to confirm screen
      confirmSel = 2;     // set selection to OK

      tft.fillRect(0, 20, 320, 240 - 20,
                   TFT_BLACK); // clear screen except top notch
      tft.setTextSize(2);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);

      tftPrint("  BACK  ", 10, 200);
      tftPrint("> OK <", 200, 200);

      openProgram();
    }
  }

  // Confirm program screen: only refreshes if buttons are pressed, great to
  // avoid EMI from contactors
  if (screen == "confirm") {
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

    // pressed back
    if (selectPressed && confirmSel == 1) {
      // shift_register.set(gasPin, LOW); // block gas contactor from being
      // energized shift_register.set(airPin, HIGH);  // air contactor to be
      // manually energized
      screen = "intro";
      tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
    }

    // pressed OK
    if (selectPressed && confirmSel == 2 && programOK) {
      controller.setSegNum(1);    // firing
      controller.setupPIDs(HIGH); // setup PID algorithm
      controller.setLastTemp();
      controller.setHeatStart(millis());
      controller.setRampStart(millis());
      controller.setProgramStart(millis()); // use later to know run duration
      tft.fillRect(0, 20, 320, 240 - 20,
                   TFT_BLACK); // clear screen except top notch
    }
  }

  // **************************
  // Settings screens
  if (strstr(screen, "settings") != NULL) {

    int settings_options;
    settings_options = 4; // (select program, action, config, done)

    // Main settings screen
    if (screen == "settings") {
      settingsScreen(settingsSel); // Display settings screen
      readButtons();

      if (upPressed && settingsSel > 1)
        settingsSel -= 1;
      if (downPressed && settingsSel < settings_options)
        settingsSel += 1;

      /* SELECTIONS */
      if (selectPressed)
        tft.fillRect(0, 20, 320, 240 - 20,
                     TFT_BLACK); // clear display except top notch
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
        screen = "settings_config";
        configSel = 1;
      }
      // user pressed DONE
      if (selectPressed && settingsSel == 4) {
        screen = "intro";
        introSel = 1;
      }
    }

    // Select program screen
    if (screen == "settings_program") {
      openProgram();
      readButtons();

      if (upPressed) {
        if (programNumber > 1) {
          programNumber -= 1;
          fileExists = true;
        }
        else screen = "settings";
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
      }
      if (downPressed) {
        programNumber += 1;
        fileExists = true;
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
      }
      if (selectPressed && programOK) {
        preferences.putInt("programNumber", programNumber); // save it in EEPROM
        programNumber = preferences.getInt("programNumber", 1); // read it back from EEPROM
        screen = "settings";
        settingsSel = 4;
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
      }
    }

    // Action screen
    if (screen == "settings_action") {
      // action = 0 means X, action = 1 means Y
      actionScreen(actionSel);
      readButtons();

      if (upPressed && actionSel != 1)
        actionSel -= 1;
      if (downPressed && actionSel != 3)
        actionSel += 1;
      if (selectPressed) {
        if (actionSel == 1)
          action = 0; // action = X
        if (actionSel == 2)
          action = 1; // action = Y
        if (actionSel == 3) {
          preferences.putInt("action", action);     // save it in EEPROM
          action = preferences.getInt("action", 0); // read it back from EEPROM
          tft.fillRect(0, 20, 320, 240 - 20,
                       TFT_BLACK); // clear screen except top notch
          screen = "settings";
          settingsSel = 4;
        }
      }
    }

    // Config screen
    if (screen == "settings_config") {
      configScreen(configSel);
      readButtons();

      // ***********************
      // This here is not GUI
      bool mode_is_captive = network.get_captive_mode();
      if (mode_is_captive) {
        network.handleCaptiveMode();
      }
      // ***********************

      if (upPressed && configSel != 1)
        configSel -= 1;
      if (downPressed && configSel != 2)
        configSel += 1;
      if (selectPressed) {
        // toggled captive mode
        if (configSel == 1) {
          network.handleCaptiveModeToggle();
        }
        // user pressed DONE
        if (configSel == 2) {
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
          screen = "settings";
          settingsSel = 4;
        }
      }
    }
  }
}

void gui_firing() {   
  
  readButtons();
  runningScreen();
  
  int segNum = controller.getSegNum();
  int segQuantity = currentProgram.segmentQuantity;

  // Up arrow button
  if (upPressed) {
  if (screenNum == 1) {
      controller.setSegNum(segQuantity + 2); // go to end of program
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
      currentProgram.segments[segNum - 1].holdingTime = currentProgram.segments[segNum - 1].holdingTime + 5;
      optionNum = 1;
      screenNum = 2;
  }

  if (optionNum == 2) {  // Add 5 deg
      currentProgram.segments[segNum - 1].targetTemperature = currentProgram.segments[segNum - 1].targetTemperature + 5;
      optionNum = 1;
      screenNum = 1;
  }

  if (optionNum == 3) {  // Goto next segment
      segNum = segNum + 1;
      optionNum = 1;
      screenNum = 2;
  }

  if (optionNum == 4) {  // SP = PV + required PID adjustment
      controller.SPequalPV();
      optionNum = 1;
      screenNum = 1;
  }

  tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
  }
}

//  INTROSCREEN: UPDATE TFT WITH INTRO MENU
void introScreen(int sel) {
  tft.setCursor(10, 70);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.print(F("pv"));
  tft.setCursor(60, 60);
  tft.setTextSize(8);
  tft.printf("%04d%c", (int)controller.getPV(), tempScale);
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

// goToIntroScreen: GOES TO INTRO SCREEN
void goToIntroScreen() {
  screen = "intro";
  introSel = 1;
  tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK); // clear screen except top notch    
}

//  SETTINGSSCREEN: UPDATE TFT WITH SETTINGS MENU
void settingsScreen(int sel) {
  char* option1 = "  SELECT PROGRAM  ";
  char* option2 = "  ACTION  ";
  char* option3 = "  CONFIG  ";
  char* option4 = "  DONE  ";

  switch (sel) {
    case 1:
      option1 = "> SELECT PROGRAM <";
      break;
    case 2:
      option2 = "> ACTION <";
      break;
    case 3:
      option3 = "> CONFIG <";
      break;      
    case 4:
      option4 = "> DONE <";
      break;
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tftPrintCenterWidth("SETTINGS", 40);
  tft.setTextSize(2);
  tftPrintCenterWidth(option1, 100);
  tftPrintCenterWidth(option2, 130);
  tftPrintCenterWidth(option3, 160);
  tft.setCursor(220, 200);
  tft.print(F(option4));
}

//  actionScreen: DISPLAYS ACTION MODE
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

//  actionScreen: DISPLAYS ACTION MODE
void configScreen(int configSel) {
  char* text1;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tftPrint("CONFIG", 2, 40);
  
  if (!network.get_captive_mode()) {
    text1 = "  START CAPTIVE PORTAL  ";
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  } 
  else {
    text1 =  "  STOP CAPTIVE PORTAL   ";
    tft.setTextColor(TFT_RED, TFT_BLACK);  
  }
  char* text2 = "  DONE  ";

  switch (configSel) {
    case 1:
      if (!network.get_captive_mode()) {
        text1 = "> START CAPTIVE PORTAL <";
      } 
      else text1 = "> STOP CAPTIVE PORTAL < ";
      break;
    case 2:
      text2 = "> DONE <";
      break;
  }
 
  tftPrintCenterWidth(text1, 100);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tftPrint(text2, 220, 200);
}

//  RUNNINGSCREEN: UPDATE TFT WITH RUNNING SCREEN
void runningScreen() {
  int segNum = controller.getSegNum();
  bool isOnHold = controller.getIsOnHold();
  double holdMins = controller.getHoldMins();

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
    tft.printf("%04d%c", (int)controller.getSP(), tempScale);
    tft.setCursor(60, 60);
    tft.setTextSize(8);
    tft.printf("%04d%c", (int)controller.getPV(), tempScale);
  }
  // Info screen
  if (screenNum == 2) {
    tft.setCursor(0, 40);
    tft.printf("PROGRAM %i: \n\n%s", programNumber, currentProgram.name.c_str());
    tft.setCursor(0, 120);
    tft.printf("SEGMENT: %i/%i", segNum, currentProgram.segmentQuantity);
    if (isOnHold == 0) {
      tft.setCursor(160, 150);
      tft.printf("Ramp to %i%c", currentProgram.segments[segNum - 1].targetTemperature, tempScale);
      tft.setCursor(160, 170);
      tft.printf("at %i%c/hr", currentProgram.segments[segNum - 1].firingRate, tempScale);
    } else {
      tft.setCursor(160, 150);
      tft.printf("Hold at %i%c \n", currentProgram.segments[segNum - 1].targetTemperature, tempScale);
      tft.setCursor(160, 170);
      tft.printf("for %.0f / %i min", holdMins, currentProgram.segments[segNum-1].holdingTime);
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

//  Updates top bar of TFT with WiFi and Influx info
void disp_top_bar() {

  if ( (millis() - topBar_start < topBarCycle)) {
    return;
  }
  int centerY = 10;
  tft.fillRect(0, 0, 320, 20, bar_color); // clear top notch
  tft.setTextSize(1);

  xSemaphoreTake(mutex, portMAX_DELAY);
  bool connected = g_connected;
  xSemaphoreGive(mutex);

  if (connected) {
    int8_t quality = network.getWifiQuality();

    tft.setTextColor(TFT_WHITE, bar_color);
    tft.drawString(String(quality) + "%", 290, centerY, 1);
    for (int8_t i = 0; i < 4; i++) {
      for (int8_t j = 0; j < 2 * (i + 1); j++) {
        if (quality > i * 25) {
          tft.drawPixel(270 + 2 * i, (centerY + 7) - j, TFT_GREEN);
        }
      }
    }
  }

  else {
    tft.setTextColor(TFT_RED, bar_color);
    tft.drawString("OFFLINE", 270, centerY, 1);
  }

  // Get published status
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool published = g_published;
  xSemaphoreGive(mutex);

  // Draw Influx check mark or cross
  if (published) {
    tft.drawLine(16, centerY + 1, 19, centerY + 4,
                 TFT_GREEN); // Draw the first diagonal line
    tft.drawLine(19, centerY + 3, 23, centerY + 0,
                 TFT_GREEN); // Draw the second diagonal line
    tft.drawLine(16, centerY + 2, 19, centerY + 5,
                 TFT_GREEN); // Draw the first inner diagonal line
    tft.drawLine(19, centerY + 4, 23, centerY + 1,
                 TFT_GREEN); // Draw the second inner diagonal line
    tft.drawLine(17, centerY + 1, 20, centerY + 4,
                 TFT_GREEN); // Draw the first outer diagonal line
    tft.drawLine(20, centerY + 3, 24, centerY + 0,
                 TFT_GREEN); // Draw the second outer diagonal line
  } else {
    tft.drawLine(17, centerY - 1, 23, centerY + 5,
                 TFT_RED); // Draw the first diagonal line
    tft.drawLine(23, centerY - 1, 17, centerY + 5,
                 TFT_RED); // Draw the second diagonal line
  }

  topBar_start = millis();
}

// disp_program: show program info on TFT
void disp_program() {
  tft.setCursor(20, 40);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.printf("SELECT PROGRAM: %i \n\n %s \n\n %s \n\n %s", programNumber, currentProgram.name.c_str(), currentProgram.duration.c_str(), currentProgram.createdDate.c_str());
}

//  disp_connecting: DISPLAYS CONNECTING MESSAGE
void disp_connecting() {
  tft.setTextColor(TFT_WHITE, bar_color);
  tft.setTextSize(1);
  tft.fillRect(0, 0, 320, 20, bar_color); // clear top notch
  tft.drawString("Connecting...", 240, 8, 1);
}

//  DISPLAYERRORMESSAGE: PRINT AN ERROR ON TFT
void disp_error_msg(char *title, char *message1, char *message2) {
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

// disp_program_error: show program error on TFT
void disp_program_error() {
    tft.setCursor(20, 40);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.print(F("SELECT PROGRAM: "));
    tft.println(programNumber);
    tft.setCursor(20, 60);
    tft.print(F("Can't find/open file"));
}

//  RESETTFT: RESETS TFT WHEN CONTACTORS ARE OPENED (EMF) OR USER CALLS IT
void resetTFT() {
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
}

//  TFTPRINTCENTERWIDTH: CENTERS CURSOR ON WIDTH AND PRINTS
void tftPrintCenterWidth(char *text, int y) {
  tft.setCursor((tftwidth - tft.textWidth(text)) / 2, y);
  tft.print(F(text));
}

//  TFTPRINT: SETS CURSOR ON (X,Y) AND PRINTS
void tftPrint(char *text, int x, int y) {
  tft.setCursor(x, y);
  tft.print(F(text));
}

//  READBUTTONS: READ IF BUTTONS ARE PRESSED
void readButtons() {
  upPressed = false;
  selectPressed = false;
  downPressed = false;

  if (digitalRead(upPin) == LOW) {
    upPressed = true;
    btnBounce(upPin);
  }
  if (digitalRead(selectPin) == LOW) {
    selectPressed = true;
    btnBounce(selectPin);
  }
  if (digitalRead(downPin) == LOW) {
    downPressed = true;
    btnBounce(downPin);
  }
}

//  BTNBOUNCE: HOLD UNTIL BUTTON IS RELEASED.  DELAY FOR ANY BOUNCE
void btnBounce(int btnPin) {
  while (digitalRead(btnPin) == LOW) {
  }
  delay(25);
}

//  openProgram: OPEN AND LOAD A FIRING PROGRAM FILE / DISPLAY ON SCREEN
void openProgram() {
  // Setup all variables
  StaticJsonDocument<2048> json;
  char filename[20];
  sprintf(filename, "/firingProgram_%d.json", programNumber);

  // Make sure you can open the file
  if (!fileExists) {
    disp_program_error;
    programOK = false;
    return;
  }

  fs::File myFile = SPIFFS.open(filename, FILE_READ); 
  // Parse the JSON file
  DeserializationError error = deserializeJson(json, myFile);

  if (!myFile || error) {
    fileExists = false;
    disp_program_error();
    programOK = false;
    return;
  }

  // Load the data into currentProgram struct
  currentProgram.name = json["name"].as<String>();
  currentProgram.duration = json["duration"].as<String>();
  currentProgram.createdDate = json["createdDate"].as<String>();

  currentProgram.segmentQuantity = json["segmentQuantity"].as<int>();
  JsonArray segmentsArray = json["segments"].as<JsonArray>();

  for (int i = 0; i < currentProgram.segmentQuantity; i++) {
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
