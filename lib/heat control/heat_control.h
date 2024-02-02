// HeatControl.h
#ifndef HEAT_CONTROL_H
#define HEAT_CONTROL_H

#include <Arduino.h>
#include <PID_v1.h>

#include "common.h"
#include "gui.h"
#include "userSetup.h"

class heat_control {
public:
  SemaphoreHandle_t &mutex;
  
  heat_control(SemaphoreHandle_t &mutex);

  void run();
  void shutDown();
  void setupPIDs(int state);
  void SPequalPV();
  void safetyCheck();

  void setSegNum(int value);
  int getSegNum() const;

  double getPV();
  double getSP();
  void setLastTemp();
  void setHeatStart(unsigned long value);
  void setRampStart(unsigned long value);
  void setProgramStart(unsigned long value);

  bool getIsOnHold() const;
  double getHoldMins() const;

private:
  void adjustHeat();
  void checkDoor();
  void updatePIDs();
  void updateSeg();

  unsigned long heatStart;
  unsigned long rampStart;
  unsigned long holdStart;
  unsigned long programStart;
  double pidInput;
  double pidOutput;
  double pidSetPoint;
  double rampHours;
  double lastRampHours;
  double holdMins;
  double lastHoldMins;
  int segQuantity;
  int lastTemp;
  int segNum;
  bool isOnHold;
  bool doorClosed;
  bool doorClosed_before;
  bool fault;
  
  PID pidCont;
};

#endif // HEAT_CONTROL_H
