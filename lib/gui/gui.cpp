#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>

#include "userSetup.h"
#include "common.h"

#include "network.h"
#include "heat_control.h"
#include "gui.h"

extern heat_control controller;
extern Network network;

static const char *TAG = "gui";

// local variables
namespace {
  String screen = "intro";     // Variable that holds screen type (start with intro)
  FiringModes firingMode;      // Auto or manual mode (0,1) 
  bool upPressed = false;      // Up button press state
  bool selectPressed = false;  // Select button press state
  bool downPressed = false;    // Down button press state
  bool programOK;              // Is the program loaded correctly?
  bool adjustingSV = false;    // Flag to know if SV is being adjusted

  int introSel = 1;            // Intro menu selected option (start or settings)
  int modeSel = 1;             // 1 is AUTOMATIC, 2 is MANUAL
  int confirmSel;              // Confirm selected option (back or OK)
  int settingsSel = 1;         // Settings menu selected setting
  int actionSel = 1;           // Option selected from action screen
  int configSel = 1;
  int tcTypeSel = 0;             // Index into tcTypes[] on TC type screen
  int screenNum = 1;           // Screen number displayed during firing (1 = temps / 2 = program info / 3 = tools / 4 = done
  int optionNum = 1;           // Option selected from screen #3
  int programNumber;           // Current firing program number.  This ties to the file name
  int action;                  // Action methods (what for?)
  int tempSV;

  unsigned long topBar_start;  // Time to start top bar refresh
  
  enum btnIndex {
    upBtn,
    selectBtn,
    downBtn
  };

  struct btnState {
    bool isPressed;
    unsigned long pressStartTime;
    int adjustStep = 1;
  };

  btnState buttonStates[3];
}

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
  firingMode = static_cast<FiringModes>(preferences.getInt("firingMode", static_cast<int>(FiringModes::automatic))); 
  tempSV = preferences.getInt("tempSV", 25);
  action = preferences.getInt("action", 0); // 0 = X
  g_tcType = (char)preferences.getInt("tcType", (int)TC_DEFAULT_TYPE);
}

void gui_run() {

  xSemaphoreTake(disp_mutex, portMAX_DELAY);

  if (!SIMULATION) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool tcFault  = g_tcFault;
    bool tcReady  = g_tcInitialized;
    char errMsg[64];
    strlcpy(errMsg, g_initErr, sizeof(errMsg));
    xSemaphoreGive(mutex);

    if (tcFault || (!tcReady && errMsg[0] != '\0')) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextSize(3);
      tftPrintCenterWidth("TC ERROR", 70);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextSize(2);
      tftPrintCenterWidth(errMsg, 130);
      tftPrintCenterWidth("Restart controller", 180);
      xSemaphoreGive(disp_mutex);
      return;
    }
  }

  disp_top_bar();

  if (controller.getSegNum() == 0) gui_idle();

  if (controller.getSegNum() >= 1) gui_firing();
  
  xSemaphoreGive(disp_mutex);

  resetCheck();

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
      if (firingMode == FiringModes::automatic) {
        screen = "confirm"; // go to confirm screen
        confirmSel = 2;     // set selection to OK

        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK); // clear screen except top notch
        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);

        tftPrint("  BACK  ", 10, 200);
        tftPrint("> OK <", 200, 200);

        openProgram();
      }
      // no confirm screen for manual mode
      else {
        controller.setMode(firingMode);
        controller.setSegNum(1);    // firing
        controller.setupPIDs(HIGH); // setup PID algorithm
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK); // clear screen except top notch
      }
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
      screen = "intro";
      tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
    }

    // pressed OK
    if (selectPressed && confirmSel == 2) {
      if (firingMode == FiringModes::automatic && programOK) {
        // could send "firing" flag as in HMI 
        controller.setMode(firingMode);
        // Send segment number to start at. Check in which segment PV already is.
        int segNum;
        for (segNum = 1; segNum <= currentProgram.segmentQuantity; segNum++) {
          int target = currentProgram.segments[segNum - 1].targetTemperature;
          if ((int)controller.getPV() < target) break;
        }
        controller.setSegNum(segNum);
        controller.setupPIDs(HIGH); // setup PID algorithm
        controller.setLastTemp();
        controller.setHeatStart(millis());
        controller.setRampStart(millis());
        controller.setProgramStart(millis()); // use later to know run duration

        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK); // clear screen except top notch
      }
    }
  }

  // **************************
  // Settings screens
  if ( screen.startsWith("settings") ) {

    int settings_options;
    settings_options = 4; // (select mode, select program, config, done)

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
      // user pressed SELECT MODE
      if (selectPressed && settingsSel == 1) {
        screen = "settings_mode";
        modeSel = 1;
      }
      // user pressed SELECT PROGRAM
      if (selectPressed && settingsSel == 2) {
        screen = "settings_program";
      }
      // user pressed CONFIG
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

    // Select mode screen
    if (screen == "settings_mode") {
      modeScreen(modeSel);
      readButtons();
      if (upPressed && modeSel > 1) {
        modeSel -= 1;
      }
      if (downPressed && modeSel < 2) {
        modeSel += 1;
      }
      if (selectPressed) {
        const char* Mode;
        if (modeSel == 1) {
          firingMode = static_cast<FiringModes>(preferences.putInt("firingMode", static_cast<int>(FiringModes::automatic))); 
        }
        if (modeSel == 2) {
          firingMode = static_cast<FiringModes>(preferences.putInt("firingMode", static_cast<int>(FiringModes::manual))); 
        }
        // update variable (idk why but yea)
        firingMode = static_cast<FiringModes>(preferences.getInt("firingMode", static_cast<int>(FiringModes::automatic))); 
        screen = "settings";
        settingsSel = 1;
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK); // clear screen except top notch
      }
    }

    // Select program screen
    if (screen == "settings_program") {
      openProgram();
      readButtons();
      int count = network.getProgramCount();

      if (upPressed) {
        if (programNumber > 1) {
          programNumber -= 1;
        }
        else screen = "settings";
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
      }
      if (downPressed) {
        if (count == 0 || programNumber < count) programNumber += 1;
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
      if (downPressed && configSel != 3)
        configSel += 1;
      if (selectPressed) {
        // toggled captive mode
        if (configSel == 1) {
          network.handleCaptiveModeToggle();
        }
        // enter TC type selection
        if (configSel == 2) {
          // seed tcTypeSel to the current g_tcType index
          static const char tcTypes[] = {'B','E','J','K','N','R','S','T'};
          tcTypeSel = 0;
          for (int i = 0; i < 8; i++) {
            if (tcTypes[i] == g_tcType) { tcTypeSel = i; break; }
          }
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
          screen = "settings_tc_type";
        }
        // user pressed DONE
        if (configSel == 3) {
          tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
          screen = "settings";
          settingsSel = 4;
        }
      }
    }

    // TC type selection screen
    if (screen == "settings_tc_type") {
      tcTypeScreen();
      readButtons();

      static const char tcTypes[] = {'B','E','J','K','N','R','S','T'};
      if (upPressed && tcTypeSel > 0)   tcTypeSel -= 1;
      if (downPressed && tcTypeSel < 7) tcTypeSel += 1;
      if (selectPressed) {
        char confirmed = tcTypes[tcTypeSel];
        preferences.putInt("tcType", (int)confirmed);
        xSemaphoreTake(mutex, portMAX_DELAY);
        g_tcType = confirmed;
        xSemaphoreGive(mutex);
        tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);
        screen = "settings_config";
        configSel = 2;
      }
    }
  }
}

void gui_firing() {   
  
  readButtons();
  runningScreen();

  if (firingMode == FiringModes:: automatic) {    
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
      // Add 5 min
      if (optionNum == 1) {  
          currentProgram.segments[segNum - 1].holdingTime = currentProgram.segments[segNum - 1].holdingTime + 5;
          optionNum = 1;
          screenNum = 2;
      }
      // Add 5 deg
      if (optionNum == 2) {  
          currentProgram.segments[segNum - 1].targetTemperature = currentProgram.segments[segNum - 1].targetTemperature + 5;
          optionNum = 1;
          screenNum = 1;
      }
      // Goto next segment
      if (optionNum == 3) {  
          controller.setSegNum(segNum + 1); 
          optionNum = 1;
          screenNum = 2;
      }
      // SP = PV + required PID adjustment  
      if (optionNum == 4) {  
          controller.SPequalPV();
          optionNum = 1;
          screenNum = 1;
      }
      tft.fillRect(0, 20, 320, 240 - 20, TFT_BLACK);  // clear screen except top notch
    }
  }

  if (firingMode == FiringModes:: manual) {
    
    if (upPressed && !adjustingSV) {
      controller.setSegNum(0); // exit firing
      controller.shutDown();
      goToIntroScreen();
    }

    if (selectPressed) {
      // save SV if user is finished adjusting it
      if (adjustingSV) {
        tempSV = preferences.putInt("tempSV", tempSV);
        tempSV = preferences.getInt("tempSV", tempSV);
        controller.setSetPoint(tempSV);
      }

      adjustingSV = !adjustingSV; // Toggle adjustment state
      selectPressed = false; // Reset flag to prevent continuous toggling
    }
    
    // Adjust SV if in adjustment state and up/down pressed
    if (adjustingSV) {
      if (upPressed) {
        tempSV += buttonStates[upBtn].adjustStep; 
      } 
      else if (downPressed) {
        tempSV -= buttonStates[downBtn].adjustStep; 
      }
    }

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
  String option1 = "  SELECT MODE  ";
  String option2 = "  SELECT PROGRAM  ";
  String option3 = "  CONFIG  ";
  String option4 = "  DONE  ";

  switch (sel) {
    case 1:
      option1 = "> SELECT MODE <";
      break;
    case 2:
      option2 = "> SELECT PROGRAM <";
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
  tft.print(option4);
}

//  modeScreen: UPDATE TFT WHEN SELECTING FIRING MODE
void modeScreen(int sel) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK); 
  tft.setTextSize(3);
  tftPrintCenterWidth("SELECT MODE", 40);
  tft.setTextSize(2);
  switch (sel) {
    case 1:
      tftPrintCenterWidth("> AUTOMATIC <", 100);
      tftPrintCenterWidth("  MANUAL  ", 140);
      break;
    case 2:
      tftPrintCenterWidth("  AUTOMATIC  ", 100);
      tftPrintCenterWidth("> MANUAL <", 140);
      break;
  }
}

//  actionScreen: DISPLAYS ACTION MODE
void actionScreen(int actionSel) {
  String text1 = "  X  ";
  String text2 = "  Y  ";
  String text3 = "  DONE  ";

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

//  configScreen: DISPLAYS CONFIG MENU
void configScreen(int configSel) {
  static const char tcTypes[] = {'B','E','J','K','N','R','S','T'};
  String text1;
  String text2;
  String text3 = "  DONE  ";

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tftPrint("CONFIG", 2, 40);

  // Show device IP address
  tft.setTextSize(1);
  String ip = network.get_captive_mode()
    ? WiFi.softAPIP().toString()
    : WiFi.localIP().toString();
  tftPrint(ip, 2, 70);
  tft.setTextSize(2);
  
  if (!network.get_captive_mode()) {
    text1 = "  START CAPTIVE PORTAL  ";
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  } 
  else {
    text1 =  "  STOP CAPTIVE PORTAL   ";
    tft.setTextColor(TFT_RED, TFT_BLACK);  
  }

  // TC type label — show current g_tcType
  char tcBuf[32];
  snprintf(tcBuf, sizeof(tcBuf), "  CHANGE TC TYPE: %c  ", g_tcType);
  text2 = String(tcBuf);

  switch (configSel) {
    case 1:
      if (!network.get_captive_mode()) {
        text1 = "> START CAPTIVE PORTAL <";
      } 
      else text1 = "> STOP CAPTIVE PORTAL < ";
      break;
    case 2: {
      char sel[32];
      snprintf(sel, sizeof(sel), "> CHANGE TC TYPE: %c <", g_tcType);
      text2 = String(sel);
      break;
    }
    case 3:
      text3 = "> DONE <";
      break;
  }
 
  tftPrintCenterWidth(text1, 100);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tftPrintCenterWidth(text2, 140);
  tftPrint(text3, 220, 200);
}

//  RUNNINGSCREEN: UPDATE TFT WITH RUNNING SCREEN
void runningScreen() {

  if (firingMode == FiringModes::automatic) {
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

      tft.setTextSize(8);
      tft.setCursor(60, 60);
      tft.printf("%04d%c", (int)controller.getPV(), tempScale);
      tft.setCursor(60, 160);
      tft.printf("%04d%c", (int)controller.getSP(), tempScale);
    }
    // Info screen
    if (screenNum == 2) {
      tft.setCursor(0, 40);
      tft.printf("PROGRAM %i: \n\n%s", programNumber, currentProgram.name.c_str());
      tft.setCursor(0, 120);
      tft.printf("SEGMENT: %i/%i", segNum, currentProgram.segmentQuantity);
      if (isOnHold == 0) {
        tft.setCursor(130, 170);
        tft.printf("Ramp to %i%c", currentProgram.segments[segNum - 1].targetTemperature, tempScale);
        tft.setCursor(130, 200);
        tft.printf("at %i%c/hr", currentProgram.segments[segNum - 1].firingRate, tempScale);
      } else {
        tft.setCursor(130, 170);
        tft.printf("Hold at %i%c \n", currentProgram.segments[segNum - 1].targetTemperature, tempScale);
        tft.setCursor(130, 200);
        tft.printf("for %.0f / %i min", holdMins, currentProgram.segments[segNum-1].holdingTime);
      }
    }
    // Tools screen
    if (screenNum == 3) {
      tftPrintCenterWidth("TOOLS:", 30);
      String option1 = "  Add 5 min  ";
      String option2 = "  Increase 5 deg  ";
      String option3 = "  Skip to next segment  ";
      String option4 = "  Equal SV and PV  ";
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

  if (firingMode == FiringModes::manual) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    
    tft.setCursor(10, 30);
    tft.print(F("pv"));
    tft.setCursor(10, 130);
    tft.print(F("sv"));

    tft.setTextSize(8);
    tft.setCursor(60, 40);
    tft.printf("%04d%c", (int)controller.getPV(), tempScale);
  
    tft.setCursor(60, 140);
    if (adjustingSV) {
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.printf("%04d%c", tempSV, tempScale);
    }
    else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.printf("%04d%c", (int)controller.getSP(), tempScale);      
    }

    // Display instructions for adjusting SV if not currently adjusting
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(130, 220);
    if (!adjustingSV) {
      tft.print(F("Press select to change SV  "));
    } else {
      tft.print(F("Press select to confirm SV "));
    }
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

  // Draw program mode in top right corner
  tft.setTextColor(TFT_GREEN, bar_color);
  if (firingMode == FiringModes::automatic) {
    tft.drawString("Automatic", 140, centerY, 1);
  } else tft.drawString("Manual", 140, centerY, 1);

  // Draw connection status
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool connecting = g_connecting;
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
  
  else if (connecting) {
    disp_connecting();
  }

  else {
    tft.setTextColor(TFT_RED, bar_color);
    tft.drawString("OFFLINE", 270, centerY, 1);
  }

  // Draw publish status
  xSemaphoreTake(mutex, portMAX_DELAY);
  bool published = g_published;
  xSemaphoreGive(mutex);

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
  int count = network.getProgramCount();
  tft.setCursor(20, 40);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.printf("PROG %i/%i:\n\n %s \n\n %s \n\n %s", programNumber, count, currentProgram.name.c_str(), currentProgram.duration.c_str(), currentProgram.createdDate.c_str());
}

//  disp_connecting: DISPLAYS CONNECTING MESSAGE
void disp_connecting() {
  tft.setTextColor(TFT_WHITE, bar_color);
  tft.setTextSize(1);
  tft.fillRect(0, 0, 320, 20, bar_color); // clear top notch
  tft.drawString("Connecting...", 240, 8, 1);
}

//  DISPLAYERRORMESSAGE: PRINT AN ERROR ON TFT
void disp_error_msg(String title, String message1, String message2) {
  xSemaphoreTake(disp_mutex, portMAX_DELAY);  // take semaphore
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(4);
  tftPrintCenterWidth("ERROR", 80);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tftPrintCenterWidth(title, 150);
  tftPrintCenterWidth(message1, 180);
  tftPrintCenterWidth(message2, 210);
  xSemaphoreGive(disp_mutex);
}

// disp_program_error: show program error on TFT
void disp_program_error() {
  tft.setCursor(20, 40);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  if (network.getProgramCount() == 0) {
    tft.print(F("No programs saved.\nUse web UI to create\na firing program."));
  } else {
    tft.print(F("PROG "));
    tft.println(programNumber);
    tft.setCursor(20, 60);
    tft.print(F("Can't open file"));
  }
}

//  RESETTFT: RESETS TFT WHEN CONTACTORS ARE OPENED (EMF) OR USER CALLS IT
void resetTFT() {
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
}

//  TFTPRINTCENTERWIDTH: CENTERS CURSOR ON WIDTH AND PRINTS
void tftPrintCenterWidth(String text, int y) {
  tft.setCursor((tftwidth - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

//  TFTPRINT: SETS CURSOR ON (X,Y) AND PRINTS
void tftPrint(String text, int x, int y) {
  tft.setCursor(x, y);
  tft.print(text);
}

// tcTypeScreen: displays TC type selection screen
void tcTypeScreen() {
  static const char tcTypes[] = {'B','E','J','K','N','R','S','T'};
  static const char* tcNames[] = {"B","E","J","K","N","R","S","T"};

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tftPrint("TC TYPE", 2, 40);

  tft.setTextSize(2);
  tftPrint("^ UP    v DOWN    SEL=OK", 2, 70);

  xSemaphoreTake(mutex, portMAX_DELAY);
  char committed = g_tcType;
  xSemaphoreGive(mutex);

  char browsed = tcTypes[tcTypeSel];
  bool changed = (browsed != committed);

  char buf[32];
  snprintf(buf, sizeof(buf), "   TC: %c   ", browsed);

  tft.setTextSize(3);
  if (changed) tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  else         tft.setTextColor(TFT_WHITE,  TFT_BLACK);
  tftPrintCenterWidth(String(buf), 130);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (changed) {
    char hint[40];
    snprintf(hint, sizeof(hint), "current: %c  (press SEL to confirm)", committed);
    tftPrintCenterWidth(String(hint), 200);
  } else {
    tftPrintCenterWidth("", 200);
  }
}

//  READBUTTONS: READ IF BUTTONS ARE PRESSED
void readButtons() {
  upPressed = false;
  selectPressed = false;
  downPressed = false;
  
  updateButtonState(upPin, upPressed, upBtn);
  updateButtonState(selectPin, selectPressed, selectBtn);
  updateButtonState(downPin, downPressed, downBtn);

}

void updateButtonState(int buttonPin, bool &buttonPressedFlag, int index) {
  if (digitalRead(buttonPin) == LOW) {
    if (!buttonStates[index].isPressed) {
      // First detection of a press
      buttonStates[index].pressStartTime = millis();
      buttonStates[index].isPressed = true;
      return;
    }
    // btn was pressed already
    else {      
      if (millis() - buttonStates[index].pressStartTime < 25) {
        // duration is below press threshold
        return;
      }
      // Press was deliberate, set flag to true
      buttonPressedFlag = true;

      // For select button or when not adjusting SV
      if (!adjustingSV || buttonPin == selectPin) {
        while (digitalRead(buttonPin) == LOW); // Wait for release
        delay(25); //  debounce delay
      } 

      // Handle long press only for up/down when adjusting SV
      if (buttonPin != selectPin && adjustingSV) { 
        // Long press detection
        int elapsed_time = millis() - buttonStates[index].pressStartTime;
        if (elapsed_time > LONG_PRESS_TIME) {
          buttonStates[index].adjustStep += (int)(elapsed_time / LONG_PRESS_TIME);
        }
      }

    }
  } 
  // reset states after release
  else {
    buttonStates[index].isPressed = false;
    buttonStates[index].adjustStep = 1; 
  }
}

//  openProgram: OPEN AND LOAD A FIRING PROGRAM FILE / DISPLAY ON SCREEN
void openProgram() {
  int count = network.getProgramCount();
  if (count == 0) {
    disp_program_error();
    programOK = false;
    return;
  }

  // Clamp to valid catalog range
  if (programNumber < 1) programNumber = 1;
  if (programNumber > count) programNumber = count;

  Network::ProgramCatalogEntry entry = network.getProgramEntry(programNumber);
  if (entry.filename.isEmpty()) {
    disp_program_error();
    programOK = false;
    return;
  }

  StaticJsonDocument<2048> json;
  fs::File myFile = SPIFFS.open(entry.filename, FILE_READ);
  if (!myFile) {
    disp_program_error();
    programOK = false;
    return;
  }

  DeserializationError error = deserializeJson(json, myFile);
  myFile.close();

  if (error) {
    disp_program_error();
    programOK = false;
    return;
  }

  currentProgram.name = json["name"] | String("Unnamed");
  currentProgram.duration = json["duration"] | String("");

  // Support both canonical (created_date) and legacy (createdDate) field names
  const char* cd = json["created_date"];
  currentProgram.createdDate = cd ? String(cd) : String(json["createdDate"] | "");

  JsonArray segmentsArray = json["segments"].as<JsonArray>();
  int segCount = 0;
  if (!segmentsArray.isNull()) {
    segCount = min((int)segmentsArray.size(), maxSegments);
    for (int i = 0; i < segCount; i++) {
      JsonObject segment = segmentsArray[i];
      int t = segment["target_temperature"] | 0;
      if (t == 0) t = segment["targetTemperature"] | 0;
      int r = segment["firing_rate"] | 0;
      if (r == 0) r = segment["firingRate"] | 0;
      int h = segment["holding_time"] | 0;
      if (h == 0) h = segment["holdingTime"] | 0;
      currentProgram.segments[i].targetTemperature = t;
      currentProgram.segments[i].firingRate = abs(r);
      currentProgram.segments[i].holdingTime = h;
      // Negative rate for downward ramp segments
      if (i >= 1 && t < currentProgram.segments[i - 1].targetTemperature)
        currentProgram.segments[i].firingRate = -abs(r);
    }
  } else {
    segCount = json["segmentQuantity"] | 0;
  }

  currentProgram.segmentQuantity = segCount;
  if (segCount == 0) {
    disp_program_error();
    programOK = false;
    return;
  }

  programOK = true;
  disp_program();
}

// Reset button: short press for TFT, 1.5s press for system
void resetCheck() {
  if (digitalRead(rstPin) == LOW) {
    unsigned long resetStart = millis();
    while (digitalRead(rstPin) == LOW && millis() - resetStart <= 2000);
    
    unsigned long pressTime = millis() - resetStart;
    
    // If pressed for 1 second or more, reset the system
    if (pressTime >= 1000) { 
      controller.shutDown();
      tft.fillScreen(TFT_BLACK);
      esp_restart();
    }
    // If pressed for 100 ms or more, reset the display
    else if (pressTime >= 100) resetTFT();  
  }
}