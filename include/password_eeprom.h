#ifndef PASSWORD_EEPROM_H
#define PASSWORD_EEPROM_H

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// Function Declarations
void initEEPROM();
void writeEEPROM(int addr, const char* data, int length);
void readEEPROM(int addr, char* data, int length);
void writePassword(const char* password);
void readPassword(char* password);
bool comparePasswords(const char* pass1, const char* pass2);
void clearInput(char* input, int length);
bool isInputComplete(const char* input, int length);
void getKeypadInput(char* input, int maxLength);
void changePassword();
void resetPassword();
void checkPassword();

// Special Mode Commands
extern const char* MODE_CHANGE_PASS;
extern const char* MODE_RESET_PASS;
extern const char* MODE_HARD_RESET;
extern const char* MODE_ADD_RFID;
extern const char* MODE_DEL_RFID;
extern const char* MODE_DEL_ALL_RFID;
extern const char* MODE_SET_RFID_TIME;
extern const char* MODE_CHANGE_ADMIN;

#endif 