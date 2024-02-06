#ifndef GUI_H
#define GUI_H

#include <Arduino.h>

void gui_start();
void gui_run();

void gui_idle();
void gui_firing();
void openProgram();

void introScreen(int sel);
void modeScreen(int modeSel);
void settingsScreen(int sel);
void actionScreen(int actionSel);
void configScreen(int configSel);
void runningScreen();

void disp_top_bar();
void disp_program();
void disp_error_msg(String title, String message1, String message2);
void disp_program_error();
void disp_connecting();

void goToIntroScreen();
void tftPrintCenterWidth(String text, int y);
void tftPrint(String text, int x, int y);
void readButtons();
void updateButtonState(int buttonPin, bool &buttonPressedFlag, int index);
// void btnBounce(int btnPin);
void resetTFT();

#endif