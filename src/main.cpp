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

// ==== Firebase ====
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ==== ĐỊNH NGHĨA CHÂN VÀ BIẾN ====
#define BUZZER_PIN 13
#define SS_PIN 5  
#define RST_PIN 0 
#define RELAY_PIN 2  
#define DHT_PIN 4      
#define FLAME_PIN 16     
#define DHT_TYPE DHT22
#define BUTTON_PIN 15     
#define TEMP_THRESHOLD 35.0

void readDHTValues();
DHT dht(DHT_PIN, DHT_TYPE);
float temperature = 0;
float humidity = 0;
bool dhtHasData = false;

unsigned long lastDhtReadMs = 0;
const unsigned long DHT_READ_INTERVAL_MS = 2000; // DHT22 nên đọc >= 2s/lần

unsigned long lastFirebaseDhtMs = 0;
const unsigned long FIREBASE_DHT_INTERVAL_MS = 10000; // đẩy lên Firebase 5s/lần

String tempPath = "sensors/temperature";
String humPath  = "sensors/humidity";

bool flameDetected = false;
bool emergencyActive = false;

// ====== LOOP TIMERS / UI STATE ======
unsigned char last_index_t = 255;

unsigned long lastNtpMs  = 0;
unsigned long lastRfidMs = 0;

const unsigned long NTP_INTERVAL_MS  = 15000; // 15 giây update NTP 1 lần
const unsigned long RFID_INTERVAL_MS = 40;    // 40ms quét RFID 1 lần

// WiFi credentials
const char* ssid = "OPPO";
const char* password_wifi = "123456789";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600);  
int startHour = 8;   
int startMin = 0;
int endHour = 18;   
int endMin = 0;
unsigned char id_rf = 0, index_t = 0;

const byte ROWS = 4; 
const byte COLS = 4; 
char hexaKeys[ROWS][COLS] = {
  {'D','C','B','A'},
  {'#','9','6','3'},
  {'0','8','5','2'},
  {'*','7','4','1'}
};
byte rowPins[ROWS] = {12, 14, 27,26}; 
byte colPins[COLS] = {25, 33, 32,17 }; 

char password[6] = "12345";
char mode_changePass[6] = "*10A#";// thay đổi mật khẩu
char mode_addRFID[6] = "*10B#";// thêm thẻ mới
char mode_delRFID[6] = "*10C#";//xóa thẻ
char mode_setRFIDTime[6] = "*10D#"; // cài thời gian cho thẻ
char data_input[6], new_pass1[6], new_pass2[6], ADMIN[6] = "99999";
unsigned char in_num = 0, error_pass = 0;
LiquidCrystal_I2C lcd(0x27, 16, 2);
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);
MFRC522 rfid(SS_PIN, RST_PIN);

#define RFID_START_ADDR 10 
#define TIME_SLOT_START (RFID_START_ADDR + 50 * 4)  
#define TIME_SLOT_SIZE 4    

byte ADMIN_UID[4] = {0xD2, 0x71, 0x38, 0x02};

// ==== Firebase objects ====
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

String dbPath = "doorControl/state";
int doorState = 0;

// ================== Âm thanh ==================
void beepSuccess() {
  tone(BUZZER_PIN, 2000, 200);
  delay(200);
  noTone(BUZZER_PIN);
}

void beepError() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 500, 200);
    delay(300);
    noTone(BUZZER_PIN);
    delay(100);
  }
}
void beepWrong() {
  for (int i = 0; i < 10; i++) {
    tone(BUZZER_PIN, 500, 200);
    delay(300);
    noTone(BUZZER_PIN);
    delay(100);
  }
}

// ================== Thời gian cho RFID ==================
bool isWithinAllowedTime(int slotIndex = -1) {
    timeClient.update();
    int currentHour = timeClient.getHours();
    int currentMin = timeClient.getMinutes();
    int currentTimeInMinutes = currentHour * 60 + currentMin;

    if (slotIndex == 255 || slotIndex ==0) {
        return true;
    }
    if (slotIndex == -1) {
        int globalStartTime = startHour * 60 + startMin;
        int globalEndTime = endHour * 60 + endMin;
        
        return (currentTimeInMinutes >= globalStartTime && 
                currentTimeInMinutes <= globalEndTime);
    } else {
        int eepromAddr = TIME_SLOT_START + slotIndex * TIME_SLOT_SIZE;
        int slotStartHour = EEPROM.read(eepromAddr);
        int slotStartMin = EEPROM.read(eepromAddr + 1);
        int slotEndHour = EEPROM.read(eepromAddr + 2);
        int slotEndMin = EEPROM.read(eepromAddr + 3);
        int slotStartTime = slotStartHour * 60 + slotStartMin;
        int slotEndTime = slotEndHour * 60 + slotEndMin;
        return (currentTimeInMinutes >= slotStartTime && 
                currentTimeInMinutes <= slotEndTime);
    }
}

// ================== EEPROM mật khẩu ==================
void writeEpprom(char data[]) {
    unsigned char i = 0;
    for (i = 0; i < 5; i++) {
        EEPROM.write(i, data[i]);
    }
    EEPROM.commit();
}
void readEpprom() {
    unsigned char i = 0;
    for (i = 0; i < 5; i++) {
        password[i] = EEPROM.read(i);
    }
}

// ================== Xử lý chuỗi nhập keypad ==================
void clear_data_input() {
    int i = 0;
    for (i = 0; i < 6; i++) {
        data_input[i] = '\0';
    }
}
unsigned char isBufferdata(char data[]) {
    unsigned char i = 0;
    for (i = 0; i < 5; i++) {
        if (data[i] == '\0') {
            return 0;
        }
    }
    return 1;
}
bool compareData(char data1[], char data2[]) {
    unsigned char i = 0;
    for (i = 0; i < 5; i++) {
        if (data1[i] != data2[i]) {
            return false;
        }
    }
    return true;
}
void insertData(char data1[], char data2[]) {
    unsigned char i = 0;
    for (i = 0; i < 5; i++) {
        data1[i] = data2[i];
    }
}
void getData() {
    char key = keypad.getKey();
    if (key) {
        // In ra serial giống code test
        Serial.print("Nhan phim: ");
        Serial.println(key);


        if (in_num < 5) {
            data_input[in_num] = key;

            // Hiển thị lên LCD
            int pass = 5 + in_num;
            lcd.setCursor(pass, 1);
            lcd.print(data_input[in_num]);

            lcd.setCursor(pass, 1);
            lcd.print("*");

            in_num++;
        }

        // Khi đủ 5 phím
        if (in_num == 5) {
            data_input[5] = '\0';          // kết thúc chuỗi để in cho chuẩn
            Serial.print("Day 5 phim: ");
            Serial.println(data_input);    // ví dụ: 12345

            // Giữ nguyên data_input để các hàm khác dùng,
            // chỉ reset biến đếm
            in_num = 0;
        }
    }
}



void Mode(int nextIndex) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter Password");
    clear_data_input();

    while (1) {
        getData();
        if (isBufferdata(data_input)) {
            if (compareData(data_input, password)) {
                lcd.clear();
                clear_data_input();
                index_t = nextIndex;
            } else {
                lcd.clear();
                lcd.setCursor(1, 1);
                lcd.print("WRONG PASSWORD");
                beepError();
                delay(1000);
                lcd.clear();
                index_t = 0;
                last_index_t = 255;   // <-- thêm dòng này để loop vẽ lại
                clear_data_input();
                in_num = 0;
            }
            break;
        }
    }
}

void checkPass() {
    getData();
    if (!isBufferdata(data_input)) return;

    if (compareData(data_input, mode_changePass)) {
        Mode(1);
    } 
    else if (compareData(data_input, mode_addRFID)) {
        Mode(8);
    } 
    else if (compareData(data_input, mode_delRFID)) {
        Mode(9);
    } 
    else if (compareData(data_input, mode_setRFIDTime)) {
        Mode(12);
    } 
    else if (compareData(data_input, password)) {
        lcd.clear();
        clear_data_input();
        in_num = 0;
        index_t = 3;
    } 
    else {
        if (error_pass == 2) {
            clear_data_input();
            lcd.clear();
            index_t = 4;
            return;
        }
        Serial.print("Error");
        lcd.clear();
        lcd.setCursor(1, 1);
        lcd.print("WRONG PASSWORD");
        beepError();
        clear_data_input();
        error_pass++;
        delay(1000);
        lcd.clear();
        index_t = 0;
        last_index_t = 255;
        clear_data_input();
        in_num = 0;
    }
}

// ================== Điều khiển cửa ==================
void openDoor() {
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("---OPENDOOR---");
    digitalWrite(RELAY_PIN, HIGH);
    beepSuccess();
    delay(5000);
    digitalWrite(RELAY_PIN, LOW);
    delay(100);  
    lcd.init();  
    lcd.backlight();
    index_t = 0;
    last_index_t = 255;

}

void error() {
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("WRONG 3 TIMES");
    beepWrong();
    delay(2000);
    lcd.setCursor(1, 1);
    lcd.print("Wait 1 minute");
    unsigned char minute = 0;
    unsigned char i = 30;
    char buff[3];
    while (i > 0) {
        if (i == 1 && minute > 0) {
            minute--;
            i = 59;
        }
        if (i == 1 && minute == 0) {
            break;
        }
        sprintf(buff, "%.2d", i);
        i--;
        delay(200);
    }
    lcd.clear();
    index_t = 0;
}

void changePass() {
    lcd.setCursor(0, 0);
    lcd.print("-- Change Pass --");
    delay(3000);
    lcd.setCursor(0, 0);
    lcd.print("--- New Pass ---");
    while (1) {
        getData();
        if (isBufferdata(data_input)) {
            insertData(new_pass1, data_input);
            clear_data_input();
            break;
        }
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("---- AGAIN ----");
    while (1) {
        getData();
        if (isBufferdata(data_input)) {
            insertData(new_pass2, data_input);
            clear_data_input();
            break;
        }
    }
    delay(1000);
    if (compareData(new_pass1, new_pass2)) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("--- Success ---");
        beepSuccess();
        delay(1000);
        writeEpprom(new_pass2);
        insertData(password, new_pass2);
        lcd.clear();
        index_t = 0;
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("-- Mismatched --");
        beepError();
        delay(1000);
        lcd.clear();
        index_t = 0;
    }
}

// ================== Nhập số 2 chữ số ==================
unsigned char numberInput() {
    char number[5] = {0};
    char count_i = 0;
    lcd.setCursor(10, 1);
    lcd.print("__");  
    
    while (count_i < 2) {
        char key = keypad.getKey();
        if (key && key >= '0' && key <= '9') {  
            number[count_i] = key;
            lcd.setCursor(10 + count_i, 1);
            lcd.print(key);
            count_i++;
            delay(300);  
        }
    }
    
    return (number[0] - '0') * 10 + (number[1] - '0');
}

// ================== RFID ==================
int findRFIDTag(byte tag[]) {
    for (int i = 0; i < 50; i++) {
        bool match = true;
        for (int j = 0; j < 4; j++) {
            byte storedByte = EEPROM.read(RFID_START_ADDR + i * 4 + j);
            if (tag[j] != storedByte) {
                match = false;
                break;
            }
        }
        if (match) {
            for (int j = 0; j < 4; j++) {
                Serial.print(" ");
            }
            Serial.println();
            return i; 
        }
    }
    return -1; 
}
int getRFIDTimeSlot(int rfidIndex) {
    if (rfidIndex < 0 || rfidIndex >= 50) {
        return -1;
    }
    int slotValue = EEPROM.read(210 + rfidIndex);
    return slotValue;
}

void rfidCheck() {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        byte rfidTag[4];
        Serial.print("RFID TAG: ");
        for (byte i = 0; i < rfid.uid.size; i++) {
            rfidTag[i] = rfid.uid.uidByte[i];
            Serial.print(rfidTag[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
        Serial.print("Current Time: ");
        Serial.print(timeClient.getHours());
        Serial.print(":");
        Serial.print(timeClient.getMinutes());
        Serial.print(":");
        Serial.println(timeClient.getSeconds());
        Serial.println(timeClient.getEpochTime());

        int rfidIndex = findRFIDTag(rfidTag);
        if (rfidIndex >= 0) {
            int timeSlot = getRFIDTimeSlot(rfidIndex);
            Serial.print("Card found with time slot: "); 
            Serial.println(timeSlot);
            
            if (timeSlot == 255) {
                Serial.println("Admin card detected - granting access");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("ADMIN ACCESS");
                lcd.setCursor(0, 1);
                lcd.print("GRANTED");
                index_t = 3;  
            } else if (isWithinAllowedTime(timeSlot)) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("ACCESS GRANTED");
                index_t = 3;  
            } else {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("ACCESS DENIED");
                lcd.setCursor(0, 1);
                lcd.print("OUTSIDE TIME");
                beepError();
                delay(2000);
                lcd.clear();
                lcd.setCursor(1, 0);
                lcd.print("Enter Password");
                clear_data_input();
                in_num = 0;

            }
        } else {
            if (error_pass == 2) {
                lcd.clear();
                index_t = 4;  
            }
            Serial.print("Error: Unknown RFID\n");
            lcd.clear();
            lcd.setCursor(3, 1);
            lcd.print("WRONG RFID");
            beepError();
            error_pass++;
            delay(1000);
            lcd.clear();
            lcd.setCursor(1, 0);
            lcd.print("Enter Password");
            clear_data_input();
            in_num = 0;

        }
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
    }
}

void addRFID() {
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print("ADD NEW RFID");
    bool validId = false;
    while (!validId) {
        lcd.clear();
        lcd.setCursor(3, 0);
        lcd.print("ADD NEW RFID");
        lcd.setCursor(0, 1);
        lcd.print("Input Id: ");
        id_rf = numberInput();
        Serial.println(id_rf);
        
        if (id_rf < 1 || id_rf > 50) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("ID OUT OF RANGE");
            lcd.setCursor(0, 1);
            lcd.print("Use ID: 01-50");
            beepError();
            delay(2000);
            continue;
        }
        
        if (id_rf != 1) {
            bool idExists = false;
            for (int i = 0; i < 4; i++) {
                if (EEPROM.read(RFID_START_ADDR + (id_rf - 1) * 4 + i) != 0) {
                    idExists = true;
                    break;
                }
            }
            
            if (idExists) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("ID ALREADY USED");
                lcd.setCursor(0, 1);
                lcd.print("Choose other ID");
                beepError();
                delay(2000);
                continue;
            }
        }
        
        if (id_rf == 1) {
            lcd.clear();
            lcd.setCursor(0, 0); 
            lcd.print("ADMIN CARD");
            lcd.setCursor(0, 1);
            lcd.print("      ");
            bool passwordCorrect = false;
            clear_data_input();
            
            unsigned long startTime = millis();
            while (millis() - startTime < 30000) { 
                getData();
                if (isBufferdata(data_input)) {
                    if (compareData(data_input, ADMIN)) {
                        passwordCorrect = true;
                        break;
                    } else {
                        lcd.clear();
                        lcd.setCursor(1, 1);
                        lcd.print("WRONG PASSWORD");
                        beepError();
                        delay(1000);
                        return;
                    }
                }
                delay(100);
            }
            
            if (!passwordCorrect) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Timeout");
                beepError();
                delay(1000);
                return;
            }
        }
        
        validId = true;
    }
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ADD NEW RFID");
    lcd.setCursor(0, 1);
    lcd.print("   Put RFID    ");
    
    byte rfidTag[4];
    bool cardRead = false;
    
    while (!cardRead) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            Serial.print("RFID TAG: ");
            for (byte i = 0; i < rfid.uid.size; i++) {
                rfidTag[i] = rfid.uid.uidByte[i];
                Serial.print(rfidTag[i], HEX);
            }
            Serial.println();

            int existingIndex = findRFIDTag(rfidTag);
            
            if (existingIndex >= 0) {
                if (id_rf == 1 && (existingIndex == 0)) {
                    cardRead = true;
                } else {
                    lcd.clear();
                    lcd.setCursor(1, 1);
                    lcd.print("RFID ADDED BF");
                    beepError();
                    delay(2000);
                    return;
                }
            } else {
                if (id_rf == 1) {
                    Serial.println("Searching for old admin cards...");
                    for (int i = 0; i < 50; i++) {
                        if (EEPROM.read(210 + i) == 255) {  
                            Serial.print("Found old admin card at index ");
                            Serial.println(i);
                            for (int j = 0; j < 4; j++) {
                                EEPROM.write(RFID_START_ADDR + i * 4 + j, 0);
                            }
                            EEPROM.write(210 + i, 0);  
                            EEPROM.commit();
                            Serial.println("Old admin card cleared");
                        }
                    }
                }
                cardRead = true;
            }
            
            rfid.PICC_HaltA();
            rfid.PCD_StopCrypto1();
        }
        delay(100);
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ADD NEW RFID");
    lcd.setCursor(0, 1);
    lcd.print("   Put Again    ");
    delay(1000);
    while (true) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            byte confirmTag[4];
            Serial.print("RFID TAG: ");
            for (byte i = 0; i < rfid.uid.size; i++) {
                confirmTag[i] = rfid.uid.uidByte[i];
                Serial.print(confirmTag[i], HEX);
            }
            // Lưu thẻ vào EEPROM
            for (int i = 0; i < 4; i++) {
                EEPROM.write(RFID_START_ADDR + (id_rf - 1) * 4 + i, confirmTag[i]);
                EEPROM.commit();
                Serial.println(EEPROM.read(RFID_START_ADDR + (id_rf - 1) * 4 + i), HEX);
            }
            
            if (id_rf == 1) {
                EEPROM.write(210 + (id_rf - 1), 255);
                EEPROM.commit();
                Serial.println("New admin card set with time slot 255");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("ADMIN CARD");
                lcd.setCursor(0, 1);
                lcd.print("SETUP COMPLETE");
            } else {
                EEPROM.write(210 + (id_rf - 1), 0);
                EEPROM.commit();
                lcd.clear();
                lcd.setCursor(0, 1);
                lcd.print("Add RFID Done");
            }
            
            beepSuccess();
            delay(2000);
            error_pass = 0; 
            index_t = 0;
            clear_data_input(); 
            Serial.print("ADD_OUT");
            lcd.clear();
            rfid.PICC_HaltA();
            rfid.PCD_StopCrypto1();
            break;
        }
        delay(100);
    }
}

void delRFID() {
    char buffDisp[20];
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("  DELETE RFID   ");
    Serial.print("DEL_IN");
    lcd.setCursor(0, 1);
    lcd.print("Scan card to delete");
    
    // Đợi quẹt thẻ
    bool cardFound = false;
    byte scannedUID[4];
    unsigned long startTime = millis();
    unsigned long timeout = 15000; // 15s
    
    while (!cardFound && (millis() - startTime < timeout)) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            for (int i = 0; i < 4; i++) {
                scannedUID[i] = rfid.uid.uidByte[i];
            }
            cardFound = true;
            Serial.print("Scanned UID: ");
            for (int i = 0; i < 4; i++) {
                Serial.print(scannedUID[i], HEX);
                Serial.print(" ");
            }
            Serial.println();
            
            rfid.PICC_HaltA();
            rfid.PCD_StopCrypto1();
        }
        delay(100);
    }
    
    if (!cardFound) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("TIMEOUT!");
        lcd.setCursor(0, 1);
        lcd.print("No card scanned");
        beepError();
        delay(2000);
        lcd.clear();
        index_t = 0;
        return;
    }
    
    int foundCardIndex = findRFIDTag(scannedUID);
    if (foundCardIndex == -1) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("CARD NOT FOUND");
        lcd.setCursor(0, 1);
        lcd.print("IN SYSTEM");
        beepError();
        delay(2000);
        lcd.clear();
        index_t = 0;
        return;
    }
    int foundCardId = foundCardIndex + 1;
    int timeSlot = getRFIDTimeSlot(foundCardIndex);
    if (timeSlot == 255 || foundCardId == 1) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("CANNOT DELETE");
        lcd.setCursor(0, 1);
        lcd.print("ADMIN CARD!");
        beepError();
        delay(2000);
        lcd.clear();
        index_t = 0;
        return;
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    sprintf(buffDisp, "Delete Card:%d", foundCardId);
    lcd.print(buffDisp);
    lcd.setCursor(0, 1);
    lcd.print("Confirming...");
    delay(1500);
    
    for (int i = 0; i < 4; i++) {
        EEPROM.write(RFID_START_ADDR + foundCardIndex * 4 + i, 0);
    }
    EEPROM.write(210 + foundCardIndex, 0);
    EEPROM.commit();

    if (timeSlot > 0 && timeSlot < 255) {
        int addr = TIME_SLOT_START + timeSlot * TIME_SLOT_SIZE;
        for (int i = 0; i < TIME_SLOT_SIZE; i++) {
            EEPROM.write(addr + i, 0);
        }
        EEPROM.commit();
        Serial.print("Cleared time slot ");
        Serial.println(timeSlot);
    }
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DELETE SUCCESS");
    sprintf(buffDisp, "Card ID:%d Deleted", foundCardId);
    lcd.setCursor(0, 1);
    lcd.print(buffDisp);
    beepSuccess();
    Serial.print("DEL_OUT - Card ID ");
    Serial.print(foundCardId);
    Serial.println(" deleted successfully");
    delay(2000);
    lcd.clear();
    error_pass = 0;
    index_t = 0;
}

void setRFIDTimeRestriction() {
  lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Set RFID Time");
    lcd.setCursor(0, 1);
    lcd.print("Start hour:");
    int startH = numberInput();
    if (startH > 23) startH = 23;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Set Time Range");
    lcd.setCursor(0, 1);
    lcd.print("Start min:");
    int startM = numberInput();
    if (startM > 59) startM = 59;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Set Time Range");
    lcd.setCursor(0, 1);
    lcd.print("End hour:");
    int endH = numberInput();
    if (endH > 23) endH = 23;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Set Time Range");
    lcd.setCursor(0, 1);
    lcd.print("End min:");
    int endM = numberInput();
    if (endM > 59) endM = 59;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Time Range:");
    char timeStr[17];
    sprintf(timeStr, "%02d:%02d-%02d:%02d", startH, startM, endH, endM);
    lcd.setCursor(0, 1);
    lcd.print(timeStr);
    delay(2000);
    
    // Quét thẻ RFID
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Scan RFID Card");
    lcd.setCursor(0, 1);
    lcd.print("to set time");
    
    unsigned long startTime = millis();
    while (millis() - startTime < 30000) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            byte rfidTag[4];
            for (byte i = 0; i < rfid.uid.size; i++) {
                rfidTag[i] = rfid.uid.uidByte[i];
            }
            
            int rfidIndex = findRFIDTag(rfidTag);
            
            if (rfidIndex >= 0) {
                // Kiểm tra admin card
                bool isAdmin = (rfidTag[0] == ADMIN_UID[0] && rfidTag[1] == ADMIN_UID[1] && 
                               rfidTag[2] == ADMIN_UID[2] && rfidTag[3] == ADMIN_UID[3]);
                
                if (isAdmin) {
                    EEPROM.write(210 + rfidIndex, 255);
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Admin Card");
                    lcd.setCursor(0, 1);
                    lcd.print("Always Access");
                } else {
                    // Tìm time slot trống
                    int timeSlot = 1;
                    for (int i = 1; i < 10; i++) {
                        bool slotUsed = false;
                        for (int j = 0; j < 50; j++) {
                            if (j != rfidIndex && EEPROM.read(210 + j) == i) {
                                slotUsed = true;
                                break;
                            }
                        }
                        if (!slotUsed) {
                            timeSlot = i;
                            break;
                        }
                    }
                    
                    // Lưu thời gian
                    int addr = TIME_SLOT_START + timeSlot * TIME_SLOT_SIZE;
                    EEPROM.write(addr, startH);
                    EEPROM.write(addr + 1, startM);
                    EEPROM.write(addr + 2, endH);
                    EEPROM.write(addr + 3, endM);
                    EEPROM.write(210 + rfidIndex, timeSlot);
                    
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("Time Updated!");
                    lcd.setCursor(0, 1);
                    lcd.print(timeStr);
                }
                
                EEPROM.commit();
                beepSuccess();
                delay(2000);
            } else {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Card not");
                lcd.setCursor(0, 1);
                lcd.print("registered!");
                beepError();
                delay(2000);
            }
            rfid.PICC_HaltA();
            rfid.PCD_StopCrypto1();
            break;
        }
        delay(100);
    }
    
    lcd.clear();
    index_t = 0;
}

// ================== Kết nối WiFi + Firebase ==================
void connectToWiFi() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    
    WiFi.begin(ssid, password_wifi);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        lcd.setCursor(attempts % 16, 1);
        lcd.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Connected");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP());
        
        timeClient.begin();
        timeClient.setTimeOffset(3600);

        // ===== Firebase setup sau khi WiFi OK =====
        config.api_key = "AIzaSyAGysnplq9UVOHezxit2BDutIM2pvK0ocQ";
        config.database_url = "https://doan2-dadd7-default-rtdb.firebaseio.com/";

        // Đăng nhập ẩn
        if (Firebase.signUp(&config, &auth, "", "")) {
            Serial.println("Firebase SignUp OK");
        } else {
            Serial.printf("Firebase SignUp failed: %s\n",
                          config.signer.signupError.message.c_str());
        }

        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
        // ==========================================

        delay(2000);
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Failed");
        lcd.setCursor(0, 1);
        lcd.print("Continuing...");
        delay(2000);
    }
    
    lcd.clear();
}

void updateNTP() {
    if (WiFi.status() == WL_CONNECTED) {
        timeClient.update();
    }
}

// ================== Emergency: lửa / quá nhiệt ==================
void checkEmergency() {
    readDHTValues(); 
    bool fire = (digitalRead(FLAME_PIN) == LOW);
    bool overTemp = (temperature > TEMP_THRESHOLD);

    if (fire || overTemp) {
        emergencyActive = true;
        digitalWrite(RELAY_PIN, HIGH);  
        lcd.clear();
        lcd.setCursor(0, 0); 
        lcd.print("FIRE DETECTED!");
        lcd.setCursor(0, 1); 
        lcd.print("EXIT!");
        beepWrong();

    } else if (emergencyActive) {
        emergencyActive = false;
        noTone(BUZZER_PIN);
        digitalWrite(RELAY_PIN, LOW);
        lcd.clear();
        lcd.setCursor(1, 0);
         lcd.print("Enter Password");
        index_t = 0;
    }
}

// ================== Đọc Firebase điều khiển cửa ==================
void checkFirebaseDoor() {
    // Nếu đang emergency thì không cho Firebase điều khiển
    if (emergencyActive) return;
    if (WiFi.status() != WL_CONNECTED) return;

    if (Firebase.ready()) {
        if (Firebase.RTDB.getInt(&fbdo, dbPath.c_str())) {
            int state = fbdo.intData();
            if (state != doorState) {
                doorState = state;
                if (doorState == 1) {
                    Serial.println("Firebase: OPEN DOOR");
                    digitalWrite(RELAY_PIN, HIGH);
                } else {
                    Serial.println("Firebase: CLOSE DOOR");
                    digitalWrite(RELAY_PIN, LOW);
                }
            }
        } else {
            Serial.printf("Firebase getInt failed: %s\n", fbdo.errorReason().c_str());
        }
    }
}
void readDHTValues() {
  if (millis() - lastDhtReadMs < DHT_READ_INTERVAL_MS) return;
  lastDhtReadMs = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity = h;

  if (!isnan(t) && !isnan(h)) dhtHasData = true;
}

void pushDHTToFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!Firebase.ready()) return;

  if (millis() - lastFirebaseDhtMs < FIREBASE_DHT_INTERVAL_MS) return;
  lastFirebaseDhtMs = millis();

  readDHTValues();
  if (!dhtHasData) return;

  bool ok1 = Firebase.RTDB.setFloat(&fbdo, tempPath.c_str(), temperature);
  bool ok2 = Firebase.RTDB.setFloat(&fbdo, humPath.c_str(),  humidity);

  if (!ok1 || !ok2) {
    Serial.printf("Firebase setFloat failed: %s\n", fbdo.errorReason().c_str());
  }
}


// ================== setup & loop ==================
void setup() {
    Serial.begin(9600);
    EEPROM.begin(512);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    SPI.begin();
    rfid.PCD_Init();
    lcd.init();
    lcd.backlight();
    lcd.print("   SYSTEM INIT   ");
    readEpprom();

// KIỂM TRA EEPROM TRỐNG
    bool empty = true;
    for (int i = 0; i < 5; i++) {
     if (password[i] != 0xFF) {
       empty = false;
       break;
     }
    }

if (empty) {
  Serial.println("EEPROM EMPTY -> SET DEFAULT PASSWORD 12345");
  char defaultPass[6] = "12345";
  writeEpprom(defaultPass);
  insertData(password, defaultPass);
}

    dht.begin();
    pinMode(FLAME_PIN, INPUT);
    connectToWiFi();
    timeClient.begin();
    timeClient.setTimeOffset(7 * 3600);
    delay(2000);
    lcd.clear();
    Serial.print("PASSWORD: ");
    Serial.println(password);
}

void loop() {
    // Ưu tiên an toàn
    checkEmergency();
    pushDHTToFirebase();     
    checkFirebaseDoor();
    if (emergencyActive) return;

    // Cập nhật NTP theo chu kỳ (không gọi liên tục)
    if (millis() - lastNtpMs >= NTP_INTERVAL_MS) {
        lastNtpMs = millis();
        updateNTP();
    }

    // Nút mở cửa (giữ như bạn đang làm)
    if (digitalRead(BUTTON_PIN) == LOW) {
        index_t = 3;
    }

    // ====== Vẽ màn Enter Password chỉ 1 lần khi vừa vào index_t==0 ======
    if (index_t == 0 && last_index_t != 0) {
        last_index_t = 0;
        lcd.clear();
        lcd.setCursor(1, 0);
        lcd.print("Enter Password");
        lcd.setCursor(0, 1);
        lcd.print("      ");
        clear_data_input();
        in_num = 0;   // reset input khi quay về màn nhập pass
    }

    // ====== State machine ======
    switch (index_t) {
        case 0:
            // đọc keypad (nên dùng getData() đã bỏ delay)
            checkPass();

            // quét RFID theo chu kỳ để không “khựng” phím
            if (millis() - lastRfidMs >= RFID_INTERVAL_MS) {
                lastRfidMs = millis();
                rfidCheck();
            }
            break;

        case 1:
            last_index_t = 1;
            changePass();
            break;

        case 3:
            last_index_t = 3;
            openDoor();
            error_pass = 0;
            break;

        case 4:
            last_index_t = 4;
            error();
            error_pass = 0;
            break;

        case 8:
            last_index_t = 8;
            addRFID();
            break;

        case 9:
            last_index_t = 9;
            delRFID();
            break;

        case 12:
            last_index_t = 12;
            setRFIDTimeRestriction();
            break;

        default:
            last_index_t = index_t;
            break;
    }
}

