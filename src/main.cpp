#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <Firebase_ESP_Client.h>

// ---------------- HARDWARE ----------------
#define BUZZER_PIN 13
#define SS_PIN 5
#define RST_PIN 0
#define RELAY_PIN 2
#define DHT_PIN 16
#define DHT_TYPE DHT22
#define FLAME_PIN 4
#define BUTTON_PIN 15
#define TEMP_THRESHOLD 35.0

float temperature = 0;
float humidity = 0;
bool emergencyActive = false;

// ---------------- WIFI ----------------
const char* ssid = "OPPO";
const char* password_wifi = "123456789";

// ---------------- NTP TIME ----------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600);

// ---------------- LCD ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- DHT22 ----------------
DHT dht(DHT_PIN, DHT_TYPE);

// ---------------- RFID ----------------
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------------- KEYPAD ----------------
const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {12, 14, 27, 26};
byte colPins[COLS] = {25, 33, 32, 17};
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// ---------------- PASSWORD + EEPROM ----------------
char password[6] = "12345";
char data_input[6];
unsigned char in_num = 0;
unsigned char error_pass = 0;
unsigned char index_t = 0;

void clear_data_input() {
  for (int i = 0; i < 6; i++) data_input[i] = '\0';
  in_num = 0;
}

bool isBufferdata(char data[]) {
  for (int i = 0; i < 5; i++)
    if (data[i] == '\0') return false;
  return true;
}

bool compareData(char d1[], char d2[]) {
  for (int i = 0; i < 5; i++)
    if (d1[i] != d2[i]) return false;
  return true;
}

// ---------------- FIREBASE ----------------
#define API_KEY "AIzaSyAGysnplq9UVOHezxit2BDutIM2pvK0ocQ"
#define DATABASE_URL "https://doan2-dadd7-default-rtdb.firebaseio.com/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long lastFirebaseCheck = 0;
unsigned long lastDHTPush = 0;

// ======================================================
//                     WIFI INIT
// ======================================================
void connectWiFi() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Connecting WiFi");
  WiFi.begin(ssid, password_wifi);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 40) {
    delay(300);
    lcd.setCursor(timeout % 16, 1);
    lcd.print(".");
    timeout++;
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi OK");
    lcd.setCursor(0,1);
    lcd.print(WiFi.localIP());
  } else {
    lcd.print("WiFi Failed!");
  }
  delay(800);
}

// ======================================================
//                  FIREBASE INIT
// ======================================================
void initFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// ----------------- Constants for EEPROM/RFID -----------------
#define RFID_START_ADDR 10
#define TIME_SLOT_START (RFID_START_ADDR + 50 * 4) // = 210
#define TIME_SLOT_SIZE 4

byte ADMIN_UID[4] = {0xAE, 0x58, 0xF9, 0x04};
char ADMIN[6] = "99999";

// ----------------- Beep helpers -----------------
void beepSuccess() {
  tone(BUZZER_PIN, 2000, 150);
  delay(150);
  noTone(BUZZER_PIN);
}
void beepError() {
  for (int i = 0; i < 2; i++) {
    tone(BUZZER_PIN, 800, 120);
    delay(180);
    noTone(BUZZER_PIN);
    delay(80);
  }
}
void beepWrong() {
  for (int i = 0; i < 6; i++) {
    tone(BUZZER_PIN, 500, 150);
    delay(220);
    noTone(BUZZER_PIN);
    delay(80);
  }
}

// ----------------- EEPROM read/write -----------------
void writeEepromPassword(char data[]) {
  for (unsigned char i = 0; i < 5; i++) {
    EEPROM.write(i, data[i]);
  }
  EEPROM.commit();
}
void readEepromPassword() {
  for (unsigned char i = 0; i < 5; i++) {
    char c = EEPROM.read(i);
    if (c == 0xFF || c == 0) c = '\0';
    password[i] = c;
  }
  // ensure null-terminated
  password[5] = '\0';
}

// ----------------- Keypad input functions -----------------
void getData() {
  char key = keypad.getKey();
  if (!key) return;
  delay(80);
  if (in_num < 5 && key >= '0' && key <= '9') {
    data_input[in_num] = key;
    lcd.setCursor(5 + in_num, 1);
    lcd.print('*');
    in_num++;
  }
  if (in_num == 5) {
    data_input[5] = '\0';
    // leave caller to process buffer
  }
}

unsigned char numberInput() {
  char number[3] = {'0', '0', '\0'};
  int count = 0;
  lcd.setCursor(10, 1);
  lcd.print("__");
  unsigned long start = millis();
  while (count < 2 && millis() - start < 30000) {
    char k = keypad.getKey();
    if (k && k >= '0' && k <= '9') {
      number[count] = k;
      lcd.setCursor(10 + count, 1);
      lcd.print(k);
      count++;
      delay(250);
    }
  }
  if (count < 2) return 0;
  return (unsigned char)((number[0] - '0') * 10 + (number[1] - '0'));
}

// ----------------- Time check (global or per-card) -----------------
bool isWithinAllowedTime(int slotIndex = -1) {
  if (WiFi.status() == WL_CONNECTED) timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMin = timeClient.getMinutes();
  int currentMinutes = currentHour * 60 + currentMin;

  if (slotIndex == 255 || slotIndex == 0) return true;

  if (slotIndex == -1) {
    // default global window (if you want to use global vars, define them earlier)
    int globalStart = 8 * 60 + 0;   // 08:00
    int globalEnd = 18 * 60 + 0;    // 18:00
    return (currentMinutes >= globalStart && currentMinutes <= globalEnd);
  } else {
    int addr = TIME_SLOT_START + slotIndex * TIME_SLOT_SIZE;
    int sh = EEPROM.read(addr);
    int sm = EEPROM.read(addr + 1);
    int eh = EEPROM.read(addr + 2);
    int em = EEPROM.read(addr + 3);
    int start = sh * 60 + sm;
    int end = eh * 60 + em;
    if (start == 0 && end == 0) return false;
    return (currentMinutes >= start && currentMinutes <= end);
  }
}

// ----------------- RFID EEPROM management -----------------
int findRFIDTag(byte tag[]) {
  for (int i = 0; i < 50; i++) {
    bool match = true;
    for (int j = 0; j < 4; j++) {
      byte stored = EEPROM.read(RFID_START_ADDR + i * 4 + j);
      if (stored == 0xFF) stored = 0;
      if (tag[j] != stored) {
        match = false;
        break;
      }
    }
    if (match) return i;
  }
  return -1;
}

int getRFIDTimeSlot(int rfidIndex) {
  if (rfidIndex < 0 || rfidIndex >= 50) return -1;
  int slot = EEPROM.read(TIME_SLOT_START - 1 + 1 + rfidIndex); // safer read
  // in our layout we stored timeslot at address 210 + idx
  int slotAddr = 210 + rfidIndex;
  int s = EEPROM.read(slotAddr);
  return s;
}

// ----------------- Local open door / lockout / change pass -----------------
void openDoor_local() {
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("---OPENDOOR---");
  digitalWrite(RELAY_PIN, HIGH);
  beepSuccess();
  delay(5000);
  digitalWrite(RELAY_PIN, LOW);
  delay(100);
  lcd.clear();
  index_t = 0;

  if (Firebase.ready()) {
    Firebase.RTDB.setInt(&fbdo, "/doorControl/state", 0);
    Firebase.RTDB.setString(&fbdo, "/doorControl/lastAction", "local_open");
  }
}

void error_lockout() {
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("WRONG 3 TIMES");
  beepWrong();
  delay(2000);
  lcd.setCursor(1, 1);
  lcd.print("Wait 1 minute");
  unsigned long start = millis();
  while (millis() - start < 60000UL) {
    delay(200);
  }
  lcd.clear();
  index_t = 0;
}

void changePass() {
  char new_pass1[6] = {0}, new_pass2[6] = {0};
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("-- Change Pass --");
  delay(600);
  lcd.clear();
  lcd.print("New pass:");
  clear_data_input();
  unsigned long start = millis();
  while (!isBufferdata(data_input) && millis()-start < 30000) {
    getData();
    delay(50);
  }
  if (!isBufferdata(data_input)) {
    lcd.clear(); lcd.print("Timeout"); beepError(); delay(800); index_t=0; return;
  }
  for (int i=0;i<5;i++) new_pass1[i]=data_input[i];
  clear_data_input();
  lcd.clear(); lcd.print("Confirm:");
  start = millis();
  while (!isBufferdata(data_input) && millis()-start < 30000) {
    getData();
    delay(50);
  }
  if (!isBufferdata(data_input)) {
    lcd.clear(); lcd.print("Timeout"); beepError(); delay(800); index_t=0; return;
  }
  for (int i=0;i<5;i++) new_pass2[i]=data_input[i];

  if (compareData(new_pass1, new_pass2)) {
    writeEepromPassword(new_pass2);
    for (int i=0;i<5;i++) password[i] = new_pass2[i];
    password[5] = '\0';
    lcd.clear(); lcd.print("Pass updated");
    beepSuccess();
    delay(1200);
    index_t = 0;
  } else {
    lcd.clear(); lcd.print("Mismatch");
    beepError();
    delay(1000);
    index_t = 0;
  }
}

// ----------------- Add RFID -----------------
void addRFID() {
  lcd.clear(); lcd.setCursor(3,0); lcd.print("ADD NEW RFID");
  lcd.setCursor(0,1); lcd.print("Input ID 01-50");
  delay(800);
  int id = numberInput();
  if (id < 1 || id > 50) {
    lcd.clear(); lcd.print("ID invalid"); beepError(); delay(800); index_t=0; return;
  }

  int idx = id - 1;
  bool slotUsed = false;
  for (int i=0;i<4;i++) {
    if (EEPROM.read(RFID_START_ADDR + idx*4 + i) != 0) { slotUsed = true; break; }
  }
  if (slotUsed) {
    lcd.clear(); lcd.print("Slot used"); beepError(); delay(800); index_t=0; return;
  }

  lcd.clear(); lcd.setCursor(0,0); lcd.print("Place card...");
  unsigned long start = millis();
  bool gotCard = false;
  byte candidate[4] = {0};
  while (millis() - start < 30000 && !gotCard) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      for (byte i=0;i<4;i++) {
        candidate[i] = rfid.uid.uidByte[i];
      }
      gotCard = true;
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
    delay(80);
  }
  if (!gotCard) {
    lcd.clear(); lcd.print("No card"); beepError(); delay(800); index_t=0; return;
  }

  // check duplicate
  int found = findRFIDTag(candidate);
  if (found >= 0) {
    lcd.clear(); lcd.print("Card exists"); beepError(); delay(900); index_t=0; return;
  }

  // write to EEPROM
  for (int i=0;i<4;i++) EEPROM.write(RFID_START_ADDR + idx*4 + i, candidate[i]);
  EEPROM.write(210 + idx, 0); // default timeslot 0 (no restriction)
  EEPROM.commit();

  lcd.clear(); lcd.print("Added"); beepSuccess(); delay(800);
  index_t = 0;
}

// ----------------- Delete RFID -----------------
void delRFID() {
  lcd.clear();
  lcd.setCursor(1,0); lcd.print("DELETE RFID");
  lcd.setCursor(0,1); lcd.print("Scan card...");
  unsigned long start = millis();
  bool foundCard = false;
  byte scanned[4] = {0};
  while (millis() - start < 15000 && !foundCard) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      for (int i=0;i<4;i++) scanned[i] = rfid.uid.uidByte[i];
      foundCard = true;
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
    delay(80);
  }
  if (!foundCard) {
    lcd.clear(); lcd.print("Timeout"); beepError(); delay(800); index_t=0; return;
  }

  int idx = findRFIDTag(scanned);
  if (idx == -1) {
    lcd.clear(); lcd.print("Not found"); beepError(); delay(800); index_t=0; return;
  }
  if (idx == 0) {
    lcd.clear(); lcd.print("Admin can't del"); beepError(); delay(1200); index_t=0; return;
  }

  // clear
  for (int i=0;i<4;i++) EEPROM.write(RFID_START_ADDR + idx*4 + i, 0);
  EEPROM.write(210 + idx, 0);
  EEPROM.commit();

  lcd.clear(); lcd.print("Deleted"); beepSuccess(); delay(800);
  index_t = 0;
}

// ----------------- Set RFID timeslot -----------------
void setRFIDTimeRestriction() {
  lcd.clear(); lcd.print("Set TimeRange");
  lcd.setCursor(0,1); lcd.print("Start H:");
  int sh = numberInput(); if (sh > 23) sh = 23;
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Set TimeRange");
  lcd.setCursor(0,1); lcd.print("Start M:");
  int sm = numberInput(); if (sm > 59) sm = 59;
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Set TimeRange");
  lcd.setCursor(0,1); lcd.print("End H:");
  int eh = numberInput(); if (eh > 23) eh = 23;
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Set TimeRange");
  lcd.setCursor(0,1); lcd.print("End M:");
  int em = numberInput(); if (em > 59) em = 59;

  lcd.clear(); lcd.print("Scan card...");
  unsigned long start = millis();
  bool got = false;
  byte tag[4] = {0};
  while (millis() - start < 30000 && !got) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      for (int i=0;i<4;i++) tag[i] = rfid.uid.uidByte[i];
      got = true;
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
    delay(80);
  }
  if (!got) { lcd.clear(); lcd.print("Timeout"); beepError(); delay(800); index_t=0; return; }

  int idx = findRFIDTag(tag);
  if (idx == -1) { lcd.clear(); lcd.print("Not registered"); beepError(); delay(800); index_t=0; return; }

  // find empty timeslot index (1..9)
  int timeslot = 1;
  for (int t = 1; t < 10; t++) {
    bool used = false;
    for (int j = 0; j < 50; j++) {
      if (EEPROM.read(210 + j) == t) { used = true; break; }
    }
    if (!used) { timeslot = t; break; }
  }

  int addr = TIME_SLOT_START + timeslot * TIME_SLOT_SIZE;
  EEPROM.write(addr, sh);
  EEPROM.write(addr + 1, sm);
  EEPROM.write(addr + 2, eh);
  EEPROM.write(addr + 3, em);
  EEPROM.write(210 + idx, timeslot);
  EEPROM.commit();

  lcd.clear(); lcd.print("Time set"); beepSuccess(); delay(800);
  index_t = 0;
}

// ----------------- RFID check on scan -----------------
void rfidCheck() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    byte tag[4] = {0};
    for (byte i = 0; i < 4; i++) {
      tag[i] = rfid.uid.uidByte[i];
      Serial.print(tag[i], HEX); Serial.print(' ');
    }
    Serial.println();

    int idx = findRFIDTag(tag);
    if (idx >= 0) {
      int slot = EEPROM.read(210 + idx);
      if (slot == 255) {
        lcd.clear(); lcd.print("ADMIN ACCESS");
        index_t = 3;
      } else if (isWithinAllowedTime(slot)) {
        lcd.clear(); lcd.print("ACCESS GRANTED");
        index_t = 3;
      } else {
        lcd.clear(); lcd.print("ACCESS DENIED");
        lcd.setCursor(0,1); lcd.print("OUT OF TIME");
        beepError();
        delay(1200);
        lcd.clear();
      }
    } else {
      error_pass++;
      lcd.clear(); lcd.print("WRONG RFID");
      beepError();
      delay(800);
      lcd.clear();
    }

    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
  }
}

// ----------------- Emergency check (basic) -----------------
void checkEmergency() {
  float t = dht.readTemperature();
  if (!isnan(t)) temperature = t;
  bool fire = (digitalRead(FLAME_PIN) == LOW);
  bool overTemp = (temperature > TEMP_THRESHOLD);

  if ((fire || overTemp) && !emergencyActive) {
    emergencyActive = true;
    digitalWrite(RELAY_PIN, HIGH);
    lcd.clear(); lcd.setCursor(0,0); lcd.print("FIRE DETECTED!");
    lcd.setCursor(0,1); lcd.print("EXIT!");
    beepWrong();

    if (Firebase.ready()) {
      Firebase.RTDB.setInt(&fbdo, "/doorControl/emergency", 1);
      Firebase.RTDB.setFloat(&fbdo, "/doorControl/temperature", temperature);
    }
  } else if (!fire && !overTemp && emergencyActive) {
    emergencyActive = false;
    noTone(BUZZER_PIN);
    digitalWrite(RELAY_PIN, LOW);
    lcd.clear(); lcd.setCursor(1,0); lcd.print("Enter Password");
    index_t = 0;
    if (Firebase.ready()) {
      Firebase.RTDB.setInt(&fbdo, "/doorControl/emergency", 0);
      Firebase.RTDB.setFloat(&fbdo, "/doorControl/temperature", temperature);
    }
  }
}

void updateDHTToFirebase() {
  if (millis() - lastDHTPush < 5000) return;
  lastDHTPush = millis();

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) return;

  temperature = t;
  humidity = h;

  if (Firebase.ready()) {
    Firebase.RTDB.setFloat(&fbdo, "/doorControl/temperature", t);
    Firebase.RTDB.setFloat(&fbdo, "/doorControl/humidity", h);
  }

  Serial.printf("DHT → %.1f°C  %.1f%%\n", t, h);
}

// ======================================================
//          Firebase Remote Door Control: /state
// ======================================================
void firebaseDoorControl() {
  if (millis() - lastFirebaseCheck < 1200) return;
  lastFirebaseCheck = millis();

  if (!Firebase.ready()) return;

  if (Firebase.RTDB.getInt(&fbdo, "/doorControl/state")) {
    int state = fbdo.intData();

    if (state == 1) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("REMOTE OPEN");

      digitalWrite(RELAY_PIN, HIGH);
      delay(5000);
      digitalWrite(RELAY_PIN, LOW);

      Firebase.RTDB.setInt(&fbdo, "/doorControl/state", 0);
      Firebase.RTDB.setString(&fbdo, "/doorControl/lastAction", "remote_open");
    }
  }
}

// ======================================================
//                   PASSWORD CHECK
// ======================================================
void Mode(int nextIndex) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Enter Password");
  clear_data_input();

  unsigned long start = millis();
  while (millis() - start < 15000) {
    getData();
    if (isBufferdata(data_input)) {
      if (compareData(data_input, password)) {
        lcd.clear();
        index_t = nextIndex;
      } else {
        lcd.clear();
        lcd.print("WRONG PASS");
        beepError();
        delay(1000);
        index_t = 0;
      }
      clear_data_input();
      return;
    }
    delay(20);
  }

  lcd.clear();
  lcd.print("Timeout");
  delay(1000);
  index_t = 0;
}

void checkPass() {
  getData();
  if (!isBufferdata(data_input)) return;

  if (compareData(data_input, password)) {
    lcd.clear(); clear_data_input();
    index_t = 3;
  } else {
    error_pass++;
    lcd.clear(); lcd.print("WRONG PASS");
    beepError();
    clear_data_input();
    delay(1000);
    if (error_pass >= 3) index_t = 4;
    else index_t = 0;
  }
}

// ======================================================
//                     SETUP
// ======================================================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(FLAME_PIN, INPUT);

  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();

  dht.begin();
  readEepromPassword();

  lcd.clear(); lcd.print("SYSTEM INIT...");
  delay(800);

  connectWiFi();
  initFirebase();

  timeClient.begin();
  timeClient.setTimeOffset(7 * 3600);

  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("Enter Password");
}

// ======================================================
//                     MAIN LOOP
// ======================================================
void loop() {

  // --- EMERGENCY CHECK FIRST ---
  checkEmergency();

  if (!emergencyActive) {

    // Update NTP time if WiFi OK
    if (WiFi.status() == WL_CONNECTED) timeClient.update();

    // Show default prompt
    if (index_t == 0) {
      lcd.setCursor(1,0);
      lcd.print("Enter Password");
      checkPass();
      rfidCheck();
    }

    // Manual button open
    if (digitalRead(BUTTON_PIN) == LOW) {
      index_t = 3;
    }

    // Switch actions
    if (index_t == 1) changePass();
    else if (index_t == 3) { openDoor_local(); error_pass = 0; }
    else if (index_t == 4) { error_lockout(); error_pass = 0; }
    else if (index_t == 8) addRFID();
    else if (index_t == 9) delRFID();
    else if (index_t == 12) setRFIDTimeRestriction();

    // Firebase tasks
    updateDHTToFirebase();
    firebaseDoorControl();
  }

  delay(50);
}
