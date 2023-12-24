#include <Arduino.h>
#include "TFT_eSPI.h"
#include "SPI.h"
#include "display_task.h"
#include "userSetup.h"

void display_task(void *pvParameter)
{
  // Get the TFT_eSPI instance
  TFT_eSPI tft = TFT_eSPI();
  // Initialize the display
  tft.init();
  tft.setRotation(1);
  // Clear the screen
  tft.fillScreen(TFT_BLACK);
  // Set "cursor" at top left corner of display (0,0) and select font 2
  tft.setCursor(0, 0, 2);
  // Set the font colour to be white with a black background
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // We can now plot text on screen using the "print" class
  tft.println("Hello World!");
  // Print the temperature
  tft.println("Temperature: ");
  
  for (;;) {
    // Print the temperature
    tft.println("Temperature: ");
    // Wait 1 second
    delay(1000);
  }
}
