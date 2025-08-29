#ifndef WIFI_TIME_H
#define WIFI_TIME_H

#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "config.h"

// Function Declarations
void initWiFi();
void connectToWiFi();
void updateNTP();
bool isWithinTimeRestriction(int slotIndex = -1);
void setTimeRestriction(int startHour, int startMin, int endHour, int endMin);
void getCurrentTime(int& hour, int& minute);

// Global Variables
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
extern bool timeRestrictionEnabled;

#endif 