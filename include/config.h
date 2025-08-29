#ifndef CONFIG_H
#define CONFIG_H

// Pin Definitions
#define BUZZER_PIN 13
#define SS_PIN 5
#define RST_PIN 0
#define RELAY_PIN 15

// WiFi Configuration
#define WIFI_SSID "Iphone 13 Pro"
#define WIFI_PASSWORD "11111111"

// EEPROM Addresses
#define RFID_START_ADDR 10
#define TIME_SLOT_START (RFID_START_ADDR + 50 * 4)
#define TIME_SLOT_SIZE 4
#define ADMIN_CARD_START_ADDR 300

// Default Values
#define DEFAULT_ADMIN_CARD {0xAE, 0x58, 0xF9, 0x04}
#define DEFAULT_PASSWORD "12345"

// Mode Definitions
enum Mode {
    MODE_IDLE,
    MODE_CHANGE_PASS,
    MODE_RESET_PASS,
    MODE_OPEN_DOOR,
    MODE_ERROR,
    MODE_ADD_RFID,
    MODE_DEL_RFID,
    MODE_DEL_ALL_RFID,
    MODE_CHANGE_ADMIN,
    MODE_SET_RFID_TIME
};

// RFID States
enum RFIDState {
    RFID_ADD,
    RFID_FIRST,
    RFID_SECOND
};

// Time Constants
#define MAX_RFID_SLOTS 10
#define MAX_RFID_TAGS 50
#define PASSWORD_LENGTH 5

#endif 