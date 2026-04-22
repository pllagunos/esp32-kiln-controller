#include "heat_control.h"

heat_control::heat_control(SemaphoreHandle_t &mutex) 
: mutex(mutex),
  pidCont(&pidInput, &pidOutput, &pidSetPoint, Kp, Ki, Kd, DIRECT) {
}

void heat_control::run() {
  safetyCheck();

  // Do heating cycle if there is a segment to run
  if (segNum >= 1) {
    checkDoor();
    updatePIDs();
    adjustHeat();
    updateSeg();
    if (SIMULATION) logData();
  }
  else {
    digitalWrite(relayPin, HIGH); 
  }

  // Synchonize shared task variables
  xSemaphoreTake(mutex, portMAX_DELAY);  // Wait for the semaphore to become available
  pidInput = g_pidInput;
  fault = g_tcFault;
  g_pidOutput = pidOutput;
  g_pidSetPoint = pidSetPoint;
  g_segNum = segNum;
  xSemaphoreGive(mutex);  // Release the semaphore
}

// adjustHeat: CONTROL THE HEATING ELEMENT
void heat_control::adjustHeat() {
  if (millis() - heatStart >= heatingCycle) { 
    heatStart = millis();
  } 
  // door must be closed
  if (!doorClosed) {
    digitalWrite(relayPin, HIGH);  // disconnect power
    return;
  }

  digitalWrite(relayPin, LOW); // enable power
  if (pidOutput * heatingCycle >= millis() - heatStart) {
    digitalWrite(heaterPin, HIGH);
  } else {
    digitalWrite(heaterPin, LOW);
  }
}

//  SHUTDOWN: SHUT DOWN SYSTEM
void heat_control::shutDown() {
  // Disconnect power
  digitalWrite(relayPin, HIGH);
  // Turn off heating element relay
  digitalWrite(heaterPin, LOW);
  // Turn off PID algorithm
  setupPIDs(LOW);
}

// SAFETYCHECK: CHECK IF TEMP IS TOO HIGH
void heat_control::safetyCheck() {
  if (fault) {
    shutDown();
    disp_error_msg("Thermocouple Error", "System was shut down", "Press RESET to restart");
    while (1) { // or while fault?
      if (digitalRead(rstPin) == LOW)
        esp_restart();
    }
  }

  if (pidInput >= maxTemp) {
    shutDown();
    disp_error_msg("MAX TEMP REACHED", "System was shut down.",
                  "Press RESET to restart.");
    while (1) {
      if (digitalRead(rstPin) == LOW)
        esp_restart();
    }
  }
}

//  CHECKDOORISR: CHECK DOOR
void heat_control::checkDoor() {
  doorClosed_before = doorClosed;            // save previous door state
  if (digitalRead(limitSwitchPin) == LOW) {  // check new door state
    doorClosed = true;                       // door closed
  } else doorClosed = false;                 // door open

  // Resume time measurements in PID algorithm
  if (!doorClosed_before && doorClosed && segNum >= 1) {  // only if door was opened and now it's closed (and firing)
    if (!isOnHold) {
      rampStart = millis() - (lastRampHours * 3600000);
      double newRampHours = (millis() - rampStart) / 3600000.0;
      // log_i("new rampHours: %.6f \n", newRampHours);
    } else {
      holdStart = millis() - (lastHoldMins * 60000);
      holdMins = (millis() - holdStart) / 60000.0;
      // log_i("holdMins: %.0f \n", holdMins);
    }
  }
}

//  UPDATEPIDS: UPDATE THE PID LOOPS
void heat_control::updatePIDs() {
  // Update the PID controller based on new variables 
  pidCont.Compute();  // (internally it will only compute if sample time has been elapsed)

  if (firingMode == FiringModes::manual) {
    pidSetPoint = manualSetPoint;
    return;
  }

  static double calcSetPoint;   // Calculated set point (degrees)

  // If door is open, exit and save ramp time (hold time saved in updateSeg)
  if (!doorClosed) {
    // If before it was closed, pause ramp time measurement (when ramping)
    if (doorClosed_before && !isOnHold) {
      lastRampHours = (millis() - rampStart) / 3600000.0;
      // log_i("lastRampHours saved. last rampHours: %.6f \n", lastRampHours);
    }
    return;
  }

  // Get the last target temperature
  if (segNum != 1) lastTemp = currentProgram.segments[segNum - 2].targetTemperature;
  // Calculate the new setpoint value.  Don't set above / below target temp
  if (isOnHold == false) {
    // Ramp: measure spanned t and calculate the SP with it
    if(SIMULATION) rampHours = (millis() - rampStart) * alpha / 3600000.0;
    else rampHours = (millis() - rampStart) / 3600000.0;
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
}

//  UPDATESEG: UPDATE THE PHASE AND SEGMENT
void heat_control::updateSeg() {

  // No need to update if not in auto mode
  if (firingMode == FiringModes::manual) {
    return;
  }

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
      if (SIMULATION) holdMins = (millis() - holdStart) * alpha / 60000.0;
      else holdMins = (millis() - holdStart) / 60000.0;
      if (holdMins >= currentProgram.segments[segNum-1].holdingTime) {
        segNum += 1;
        isOnHold = false;
        rampStart = millis();
      }
    }
  }
  // If door has just been opened (perhaps implement similar method to lastTemp = pidInput;)
  if (!doorClosed && doorClosed_before) {
    lastHoldMins = (millis() - holdStart) / 60000.0;
    // log_i("lastHoldMins saved. last holdMins: %.0f \n", lastHoldMins);
  }

  // Check if complete -> turn off all zones
  if (segNum > currentProgram.segmentQuantity) {
    shutDown();
    goToIntroScreen();
    segNum = 0; 
  }
}

//  SETUPPIDS: INITIALIZE THE PID LOOPS
void heat_control::setupPIDs(int state) {
  // standard period is equal to heating cycle
  if (SIMULATION) {
    int newSampleTime = (heatingCycle/(int)alpha);
    log_i("sample time for PID = %d \n", newSampleTime);
    pidCont.SetSampleTime(newSampleTime);
    // each PID cycle still uses delta{t} = pidCycle
		// SetSampleTime changes delta{t}, revert it using artificial tunnings
		// alpha = pidCycle / newSampleTime 
    pidCont.SetTunings(Kp, Ki*alpha, Kd/alpha);
  }
  else pidCont.SetSampleTime(heatingCycle);  
  
  pidCont.SetOutputLimits(0, 1);      // from 0%-100%
  pidOutput = 0;                        // output should start at 0% at new firings
  pidSetPoint = 0;                      // same for the setpoint
  if (state == HIGH) pidCont.SetMode(AUTOMATIC);
  if (state == LOW) pidCont.SetMode(MANUAL);
}

// Log data to SPIFFS
void heat_control::logData() {
  float elapsed_seconds = (millis()-programStart) * alpha / 1000.0; 
  int hours = (int) elapsed_seconds / 3600;
  int mins = ((int) elapsed_seconds % 3600) / 60;
  int seconds = (int) elapsed_seconds % 60;

  // Open file for appending
  // File file = SPIFFS.open("/data.csv", FILE_APPEND);
  // if (!file) {
  //   log_i("Failed to open file for appending\n");
  //   return;
  // }
  // file.printf("%02d:%02d:%02d,%.2f,%.2f,%.2f,\n", 
  //   hours, mins, seconds, pidInput, pidOutput, pidSetPoint);
  // file.close();

  // For plotting on the serial monitor
  log_i("Time:%02d:%02d:%02d,Temperature:%.2f,Power:%.2f,SetPoint:%.2f",
    hours, mins, seconds, pidInput, pidOutput, pidSetPoint);
}

// SP is set equal to PV
void heat_control::SPequalPV() {
  lastTemp = pidInput;
}

// *****************************
// Getter / Setter functions
// *****************************

void heat_control::setMode(FiringModes mode) {
  firingMode = mode;
}

double heat_control::getPV() {
  return pidInput;
}

double heat_control::getSP() {
  return pidSetPoint;
}

void heat_control::setSetPoint(int value) {
  manualSetPoint = value;
}

void heat_control::setSegNum(int value) {
  segNum = value;
  // user manually moved to next segment
  if (segNum > 1 && segNum <= currentProgram.segmentQuantity) {
    isOnHold = false;
    rampStart = millis();
  }
}

int heat_control::getSegNum() const {
  return segNum;
}

void heat_control::setLastTemp() {
  lastTemp = pidInput;
}

void heat_control::setHeatStart(unsigned long value) {
  heatStart = value;
}

void heat_control::setRampStart(unsigned long value) {
  rampStart = value;
}

void heat_control::setProgramStart(unsigned long value) {
  programStart = value;
}

bool heat_control::getIsOnHold() const {
  return isOnHold;
}

double heat_control::getHoldMins() const{
  return holdMins;
}
