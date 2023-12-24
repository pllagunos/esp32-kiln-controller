#ifndef FIRINGPROGRAM_H
#define FIRINGPROGRAM_H

#include <Arduino.h>

#define MAX_SEGMENTS 20 // Maximum number of segments in a firing program

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
  FiringSegment segments[MAX_SEGMENTS];
  int segmentQuantity;
};

#endif