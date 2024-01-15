#ifndef GUI_H
#define GUI_H

#include <Arduino.h>

void gui_start();
void gui_run();

void gui_idle();
void gui_firing();
void openProgram();

void introScreen(int sel);
void settingsScreen(int sel);
void actionScreen(int actionSel);
void configScreen(int configSel);
void runningScreen();

void disp_top_bar();
// void drawTopBar(int8_t quality, bool published, bool connected);
void disp_program();
void disp_error_msg(char* title, char* message1, char* message2);
void disp_program_error();
void disp_connecting();

void goToIntroScreen();
void tftPrintCenterWidth(char* text, int y);
void tftPrint(char* text, int x, int y);
void readButtons();
void btnBounce(int btnPin);
void resetTFT();

#endif