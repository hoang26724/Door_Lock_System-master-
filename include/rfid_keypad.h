#ifndef RFID_KEYPAD_H
#define RFID_KEYPAD_H

#include <Arduino.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"

// Keypad Configuration
const byte ROWS = 4;
const byte COLS = 4;
extern char hexaKeys[ROWS][COLS];
extern byte rowPins[ROWS];
extern byte colPins[COLS];

// Function Declarations
void initRFID();
void initKeypad();
void initLCD();
void beepSuccess();
void beepError();
bool isAdminCard(byte tag[]);
void initAdminCard();
bool isWithinAllowedTime(int slotIndex = -1);
int findRFIDTag(byte tag[]);
int getRFIDTimeSlot(int rfidIndex);
void rfidCheck();
void addRFID();
void delRFID();
void delAllRFID();
void changeAdminCard();
void setRFIDTimeRestriction();
unsigned char numberInput();
void displayMessage(const char* line1, const char* line2 = "", int delayTime = 0);

#endif 