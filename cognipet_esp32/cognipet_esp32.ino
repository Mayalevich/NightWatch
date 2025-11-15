/*
 * CogniPet: Hospital-Induced Delirium Detection Device
 * 
 * Hardware:
 * - ESP32-S3
 * - Grove LCD RGB Backlight (16x2 character LCD)
 * - 3 buttons (BTN1, BTN2, BTN3)
 * - LED for feedback
 * 
 * Features:
 * - Cognitive assessment tests (orientation, memory, attention, executive function)
 * - Virtual pet interactions (feed, play, clean)
 * - LED feedback for correct/incorrect answers
 * - RGB backlight color changes for mood/status
 * - BLE data transmission to backend
 * 
 * Libraries Required (install via Arduino Library Manager):
 * - ESP32 BLE Arduino (built-in with ESP32 board support)
 * - Preferences (built-in with ESP32)
 * 
 * Setup Instructions:
 * 1. Install ESP32 board support in Arduino IDE:
 *    - File > Preferences > Additional Board Manager URLs
 *    - Add: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 *    - Tools > Board > Boards Manager > Search "ESP32" > Install
 * 2. Select board: Tools > Board > ESP32 Arduino > ESP32S3 Dev Module
 * 3. Select port: Tools > Port > (your ESP32-S3 port)
 * 4. Upload this sketch
 */

#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

// ==== Pin Definitions ====
// ESP32-S3 I2C pins - try these common configurations:
// Option 1: GPIO 8/9 (common for ESP32-S3)
// Option 2: GPIO 21/22 (Arduino default, but may not work on S3)
// Option 3: GPIO 1/2 (your current setting)
#define SDA_PIN   8   // Changed from 1 - try GPIO 8
#define SCL_PIN   9   // Changed from 2 - try GPIO 9
#define LED_PIN   11
#define BTN1_PIN  12  // Button A / Feed
#define BTN2_PIN  13  // Button B / Play
#define BTN3_PIN  14  // Button C / Clean / Select

// ==== Grove LCD RGB Backlight Addresses ====
#define LCD_ADDR  0x3E   // text controller
#define RGB_ADDR  0x62   // backlight (PCA9633)

// ==== BLE Configuration ====
#define BLE_DEVICE_NAME "CogniPet"
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define ASSESSMENT_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define INTERACTION_UUID    "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define TIME_RESYNC_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)  // every 6 hours
#define TIME_CHECK_INTERVAL_MS 60000

const char* WIFI_SSID = "Huawei mate60 5G";
const char* WIFI_PASSWORD = "123456789";
const char* TZ_INFO = "EST5EDT,M3.2.0,M11.1.0";
const char* NTP_PRIMARY = "ca.pool.ntp.org";
const char* NTP_SECONDARY = "pool.ntp.org";
const char* NTP_TERTIARY = "time.nist.gov";

bool wifiConnectedFlag = false;
bool timeSynced = false;
unsigned long lastTimeSyncAttempt = 0;
unsigned long lastTimeMaintenance = 0;
bool wifiEverConnected = false;
unsigned long lastWifiSuccessMs = 0;
unsigned long lastNtpSyncMs = 0;
char lastWifiIp[20] = "--";

const char* DAY_SHORT[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
const char* PERIOD_SHORT[3] = {"AM", "PM", "EVE"};

// ==== Device States ====
enum DeviceState {
  STATE_FIRST_BOOT,
  STATE_ASSESSMENT,
  STATE_PET_NORMAL,
  STATE_PET_MENU,
  STATE_PET_STATS,
  STATE_PET_MOOD,
  STATE_PET_GAME,
  STATE_DIAGNOSTICS
};

enum PetMenu {
  MENU_MAIN,
  MENU_STATS,
  MENU_MOOD,
  MENU_GAMES
};

enum AssessmentPhase {
  PHASE_ORIENTATION,
  PHASE_MEMORY,
  PHASE_ATTENTION,
  PHASE_EXECUTIVE,
  PHASE_COMPLETE
};

// ==== Data Structures ====
struct AssessmentResult {
  uint32_t timestamp;
  uint8_t orientation_score;    // 0-3
  uint8_t memory_score;         // 0-3
  uint8_t attention_score;      // 0-3
  uint8_t executive_score;      // 0-3
  uint8_t total_score;          // 0-12
  uint16_t avg_response_time_ms;
  uint8_t alert_level;          // 0=green, 1=yellow, 2=orange, 3=red
};

struct PetState {
  uint8_t happiness;   // 0-100
  uint8_t hunger;      // 0-100 (100 = very hungry)
  uint8_t cleanliness; // 0-100
  unsigned long lastFed;
  unsigned long lastPlayed;
  unsigned long lastCleaned;
};

struct InteractionLog {
  uint32_t timestamp;
  uint8_t interaction_type;  // 0=feed, 1=play, 2=clean, 3=game
  uint16_t response_time_ms;
  uint8_t success;           // 1=success, 0=fail
  int8_t mood_selected;      // -1 if not mood check
};

// ==== Global Variables ====
DeviceState currentState = STATE_FIRST_BOOT;
AssessmentPhase assessmentPhase = PHASE_ORIENTATION;
AssessmentResult lastAssessment;
PetState pet;
Preferences prefs;

// BLE
BLEServer* pServer = nullptr;
BLECharacteristic* pAssessmentChar = nullptr;
BLECharacteristic* pInteractionChar = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Button state
bool btn1Pressed = false;
bool btn2Pressed = false;
bool btn3Pressed = false;
bool btn1Last = false;
bool btn2Last = false;
bool btn3Last = false;
unsigned long btn1PressTime = 0;
unsigned long btn2PressTime = 0;
unsigned long btn3PressTime = 0;

// Assessment state
uint8_t currentQuestion = 0;
uint8_t selectedAnswer = 0;
unsigned long questionStartTime = 0;
uint16_t totalResponseTime = 0;
uint8_t responseCount = 0;

// Memory test
uint8_t memorySequence[3];
uint8_t userSequence[3];
uint8_t memoryStep = 0;

// Attention test
uint8_t attentionTrials = 0;
uint8_t attentionCorrect = 0;
unsigned long attentionStartTime = 0;
bool waitingForAttention = false;

// Executive function test
uint8_t sequenceStep = 0;
uint8_t correctSequence[4] = {1, 0, 2, 3}; // Dinner, Shower, Brush, Sleep

// Interaction queue
InteractionLog interactionQueue[20];
uint8_t queueHead = 0;
uint8_t queueTail = 0;

// RGB state tracking
uint8_t lastR = 255, lastG = 255, lastB = 255; // Track last color to avoid unnecessary updates

// Menu state
PetMenu currentMenu = MENU_MAIN;
uint8_t menuSelection = 0;
unsigned long lastMenuChange = 0;
uint8_t diagnosticsPage = 0;
unsigned long lastDiagnosticsRefresh = 0;
bool diagnosticsActive = false;
const uint8_t DIAGNOSTIC_PAGE_COUNT = 4;

// ==== Timekeeping Helpers ====
bool hasWiFiCredentials() {
  return strlen(WIFI_SSID) > 0 &&
         strcmp(WIFI_SSID, "YOUR_WIFI") != 0 &&
         strlen(WIFI_PASSWORD) > 0 &&
         strcmp(WIFI_PASSWORD, "YOUR_PASSWORD") != 0;
}

bool connectToWiFi() {
  if (!hasWiFiCredentials()) {
    Serial.println("WiFi credentials not configured; skipping real-time sync.");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnectedFlag = true;
    return true;
  }

  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnectedFlag = true;
    wifiEverConnected = true;
    lastWifiSuccessMs = millis();
    IPAddress ip = WiFi.localIP();
    Serial.print("WiFi connected, IP: ");
    Serial.println(ip);
    snprintf(lastWifiIp, sizeof(lastWifiIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
  }

  wifiConnectedFlag = false;
  snprintf(lastWifiIp, sizeof(lastWifiIp), "--");
  Serial.println("WiFi connection failed.");
  return false;
}

void shutdownWiFi() {
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  wifiConnectedFlag = false;
}

bool syncTimeFromNTP() {
  lastTimeSyncAttempt = millis();
  if (!connectToWiFi()) {
    timeSynced = false;
    return false;
  }

  Serial.println("Syncing time via NTP...");
  configTzTime(TZ_INFO, NTP_PRIMARY, NTP_SECONDARY, NTP_TERTIARY);

  struct tm timeinfo;
  bool success = false;
  for (int i = 0; i < 10; i++) {
    if (getLocalTime(&timeinfo, 1000)) {
      success = true;
      break;
    }
    delay(250);
  }

  if (success) {
    timeSynced = true;
    lastNtpSyncMs = millis();
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &timeinfo);
    Serial.print("Time sync OK: ");
    Serial.println(buf);
  } else {
    Serial.println("Failed to obtain time from NTP.");
    timeSynced = false;
  }

  shutdownWiFi();  // conserve power once time is fetched
  return success;
}

void ensureTimeSync() {
  if (!hasWiFiCredentials()) {
    timeSynced = false;
    return;
  }

  if (!timeSynced) {
    syncTimeFromNTP();
    return;
  }

  if (millis() - lastTimeSyncAttempt > TIME_RESYNC_INTERVAL_MS) {
    syncTimeFromNTP();
  }
}

bool getLocalTimeSafe(struct tm* info) {
  if (!timeSynced) {
    ensureTimeSync();
    if (!timeSynced) {
      return false;
    }
  }

  if (!getLocalTime(info, 1000)) {
    Serial.println("RTC read failed; will attempt to resync.");
    timeSynced = false;
    return false;
  }

  return true;
}

uint32_t getCurrentTimestamp() {
  if (timeSynced) {
    time_t now = time(nullptr);
    if (now > 0) {
      return static_cast<uint32_t>(now);
    }
  }
  return millis();
}

void maintainTimeService() {
  if (!hasWiFiCredentials()) {
    return;
  }

  if (millis() - lastTimeMaintenance > TIME_CHECK_INTERVAL_MS) {
    ensureTimeSync();
    lastTimeMaintenance = millis();
  }
}

void initializeTimeService() {
  if (!hasWiFiCredentials()) {
    Serial.println("Skipping time sync (WiFi credentials not set).");
    return;
  }
  syncTimeFromNTP();
}

void rotateOptions(uint8_t* arr, uint8_t shift) {
  shift %= 3;
  while (shift--) {
    uint8_t tmp = arr[0];
    arr[0] = arr[1];
    arr[1] = arr[2];
    arr[2] = tmp;
  }
}

// ==== LCD Functions ====
void lcdCommand(uint8_t cmd) {
  Wire.beginTransmission(LCD_ADDR);
  Wire.write((uint8_t)0x80);  // Command mode
  Wire.write(cmd);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    Serial.print("LCD command error: ");
    Serial.println(error);
  }
  delayMicroseconds(100); // Small delay for command processing
}

void lcdData(uint8_t data) {
  Wire.beginTransmission(LCD_ADDR);
  Wire.write((uint8_t)0x40);  // Data mode
  Wire.write(data);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    Serial.print("LCD data error: ");
    Serial.println(error);
  }
  delayMicroseconds(50);
}

void lcdSetRGB(uint8_t r, uint8_t g, uint8_t b) {
  // Only update if color changed
  if (r == lastR && g == lastG && b == lastB) {
    return;
  }
  lastR = r;
  lastG = g;
  lastB = b;
  
  // Check if RGB device responds
  Wire.beginTransmission(RGB_ADDR);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    Serial.print("RGB I2C error: ");
    Serial.println(error);
    return;
  }
  
  // PCA9633 initialization - reset first
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x00);  // MODE1 register
  Wire.write(0x00);  // Normal mode, no sleep
  error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("RGB MODE1 error");
    return;
  }
  delay(2);

  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x08);  // MODE2 register
  Wire.write(0x04);  // Open-drain, non-inverted
  error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("RGB MODE2 error");
    return;
  }
  delay(2);

  // IMPORTANT: Set LEDOUT registers FIRST to enable PWM mode
  // LEDOUT0 (0x14) controls LEDs 0-3, LEDOUT1 (0x15) controls LEDs 4-7
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x14);  // LEDOUT0 register
  Wire.write(0xAA);  // 0xAA = PWM mode for all 4 LEDs (10101010)
  error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("RGB LEDOUT0 error");
    return;
  }
  delay(2);
  
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x15);  // LEDOUT1 register  
  Wire.write(0xAA);  // PWM mode for LEDs 4-7
  error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("RGB LEDOUT1 error");
    return;
  }
  delay(2);

  // Now set PWM values - PCA9633 PWM registers start at 0x02
  // Register mapping: 0x02=PWM0, 0x03=PWM1, 0x04=PWM2, etc.
  // Grove LCD typically uses: PWM2=Red, PWM3=Green, PWM4=Blue
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x04);  // PWM2 (Red)
  Wire.write(r);
  error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("RGB PWM2 (Red) error");
    return;
  }
  delay(2);
  
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x05);  // PWM3 (Green)
  Wire.write(g);
  error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("RGB PWM3 (Green) error");
    return;
  }
  delay(2);
  
  Wire.beginTransmission(RGB_ADDR);
  Wire.write(0x06);  // PWM4 (Blue)
  Wire.write(b);
  error = Wire.endTransmission();
  if (error != 0) {
    Serial.println("RGB PWM4 (Blue) error");
    return;
  }
  
  Serial.print("RGB set: R=");
  Serial.print(r);
  Serial.print(" G=");
  Serial.print(g);
  Serial.print(" B=");
  Serial.print(b);
  Serial.print(" (error=");
  Serial.print(error);
  Serial.println(")");
}

void lcdClear() {
  lcdCommand(0x01);
  delay(2);
}

void lcdHome() {
  lcdCommand(0x02);
  delay(2);
}

void lcdDisplayOn() {
  lcdCommand(0x0C);
}

void lcdSetCursor(uint8_t col, uint8_t row) {
  static const uint8_t row_offsets[] = {0x00, 0x40};
  lcdCommand(0x80 | (row_offsets[row] + col));
}

void lcdInit() {
  Serial.println("Initializing LCD...");
  
  // Check if LCD responds
  Wire.beginTransmission(LCD_ADDR);
  uint8_t error = Wire.endTransmission();
  if (error != 0) {
    Serial.print("ERROR: LCD not responding! Error code: ");
    Serial.println(error);
    Serial.println("Check I2C wiring and address (should be 0x3E)");
    return;
  }
  Serial.println("LCD responds to I2C");
  
  // Reset sequence - try multiple times
  for (int i = 0; i < 3; i++) {
    delay(100); // Longer delay for ESP32-S3
    
    // Function set: 8-bit, 2-line, 5x8 dots
  lcdCommand(0x38);
    delay(10);
    
    // Extended function set (some Grove LCDs need this)
    lcdCommand(0x39);
    delay(10);

  // Return to normal instruction set
  lcdCommand(0x38);
    delay(10);
    
    // Display on, cursor off, blink off
    lcdCommand(0x0C);
    delay(10);
    
    // Clear display
    lcdCommand(0x01);
    delay(10);

  // Entry mode: increment, no shift
  lcdCommand(0x06);
    delay(10);
  }
  
  // Test: Try to write something
  lcdSetCursor(0, 0);
  lcdPrint("Test 123");
  delay(500);
  
  Serial.println("LCD init complete - check if 'Test 123' appears");
}

void lcdPrint(const char* s) {
  uint8_t count = 0;
  while (*s && count < 16) {  // Limit to 16 chars per line
    lcdData(*s++);
    count++;
  }
}

void lcdPrintPadded(const char* s, uint8_t width) {
  uint8_t len = 0;
  const char* p = s;
  while (*p && len < width) {
    len++;
    p++;
  }
  
  // Print text
  lcdPrint(s);
  
  // Pad with spaces
  for (uint8_t i = len; i < width; i++) {
    lcdData(' ');
  }
}

void lcdPrintNum(int num) {
  char buf[10];
  snprintf(buf, sizeof(buf), "%d", num);
  lcdPrint(buf);
}

void i2cScan() {
  Serial.println("\n=== I2C Scan ===");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("Found device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      if (addr == LCD_ADDR) Serial.print(" (LCD)");
      if (addr == RGB_ADDR) Serial.print(" (RGB)");
      Serial.println();
      found++;
    } else if (error == 4) {
      Serial.print("Unknown error at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
    }
  }
  if (!found) {
    Serial.println("No I2C devices found!");
    Serial.println("Check wiring: SDA=pin 1, SCL=pin 2");
  } else {
    Serial.print("Found ");
    Serial.print(found);
    Serial.println(" device(s)");
  }
  Serial.println("================\n");
}

// ==== LED Feedback ====
void ledFlash(uint8_t times, uint16_t duration) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(LED_PIN, LOW);
    if (i < times - 1) delay(duration);
  }
}

void ledCorrect() {
  // Green flash pattern (3 quick flashes)
  ledFlash(3, 100);
}

void ledIncorrect() {
  // Red flash pattern (1 long flash)
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
}

// ==== Button Handling ====
void updateButtons() {
  btn1Last = btn1Pressed;
  btn2Last = btn2Pressed;
  btn3Last = btn3Pressed;
  
  btn1Pressed = (digitalRead(BTN1_PIN) == LOW);
  btn2Pressed = (digitalRead(BTN2_PIN) == LOW);
  btn3Pressed = (digitalRead(BTN3_PIN) == LOW);
  
  if (btn1Pressed && !btn1Last) {
    btn1PressTime = millis();
  }
  if (btn2Pressed && !btn2Last) {
    btn2PressTime = millis();
  }
  if (btn3Pressed && !btn3Last) {
    btn3PressTime = millis();
  }
}

bool buttonPressed(uint8_t btn) {
  switch(btn) {
    case 1: return btn1Pressed && !btn1Last;
    case 2: return btn2Pressed && !btn2Last;
    case 3: return btn3Pressed && !btn3Last;
    default: return false;
  }
}

// ==== Persistence ====
bool isFirstBoot() {
  prefs.begin("cognipet", true);
  bool firstBoot = prefs.getBool("firstBoot", true);
  prefs.end();
  return firstBoot;
}

void markBootComplete() {
  prefs.begin("cognipet", false);
  prefs.putBool("firstBoot", false);
  prefs.end();
}

// ==== BLE Setup ====
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE Client connected");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE Client disconnected");
  }
};

void setupBLE() {
  BLEDevice::init(BLE_DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pAssessmentChar = pService->createCharacteristic(
    ASSESSMENT_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pAssessmentChar->addDescriptor(new BLE2902());

  pInteractionChar = pService->createCharacteristic(
    INTERACTION_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pInteractionChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();
  
  Serial.println("BLE advertising started");
}

void sendAssessmentViaBLE() {
  Serial.print("Attempting to send assessment via BLE... ");
  Serial.print("deviceConnected=");
  Serial.print(deviceConnected);
  Serial.print(", pAssessmentChar=");
  Serial.println(pAssessmentChar ? "OK" : "NULL");
  
  if (deviceConnected && pAssessmentChar) {
    uint8_t data[32];
    memcpy(data, &lastAssessment, sizeof(AssessmentResult));
    pAssessmentChar->setValue(data, sizeof(AssessmentResult));
    pAssessmentChar->notify();
    Serial.println("✓ Assessment sent via BLE");
    Serial.print("  Score: ");
    Serial.print(lastAssessment.total_score);
    Serial.print("/12, Alert: ");
    Serial.println(lastAssessment.alert_level);
  } else {
    Serial.println("✗ Cannot send: device not connected or characteristic not available");
  }
}

void logInteraction(uint8_t type, uint16_t responseTime, uint8_t success, int8_t mood = -1) {
  InteractionLog log;
  log.timestamp = millis();
  log.interaction_type = type;
  log.response_time_ms = responseTime;
  log.success = success;
  log.mood_selected = mood;
  
  interactionQueue[queueTail] = log;
  queueTail = (queueTail + 1) % 20;
  
  // Send via BLE if connected
  if (deviceConnected && pInteractionChar) {
    uint8_t data[sizeof(InteractionLog)];
    memcpy(data, &log, sizeof(InteractionLog));
    pInteractionChar->setValue(data, sizeof(InteractionLog));
    pInteractionChar->notify();
  }
  
  Serial.print("Interaction logged: type=");
  Serial.print(type);
  Serial.print(" time=");
  Serial.print(responseTime);
  Serial.print(" success=");
  Serial.println(success);
}

// ==== Cognitive Assessment Tests ====
uint8_t testOrientation() {
  uint8_t score = 0;
  struct tm now;
  if (!getLocalTimeSafe(&now)) {
    Serial.println("Orientation test requires valid real-time clock. Prompting user to sync.");
    lcdClear();
    lcdSetCursor(0, 0);
    lcdPrint("Sync clock first");
    lcdSetCursor(0, 1);
    lcdPrint("Hold BTN1+BTN2");
    delay(2500);
    return 0;
  }
  
  // Question 1: Day of week
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Today is?");
  lcdSetCursor(0, 1);

  uint8_t dayOptions[3];
  uint8_t correctDaySlot = 0;
  uint8_t today = now.tm_wday % 7;
  dayOptions[0] = today;
  dayOptions[1] = (today + 6) % 7;
  dayOptions[2] = (today + 1) % 7;
  rotateOptions(dayOptions, now.tm_mday % 3);
  for (uint8_t i = 0; i < 3; i++) {
    if (dayOptions[i] == today) {
      correctDaySlot = i;
      break;
    }
  }

  char dayLine[17];
  snprintf(dayLine, sizeof(dayLine), "A:%-2sB:%-2sC:%-2s",
           DAY_SHORT[dayOptions[0]],
           DAY_SHORT[dayOptions[1]],
           DAY_SHORT[dayOptions[2]]);
  lcdPrint(dayLine);
  delay(1000);
  
  questionStartTime = millis();
  selectedAnswer = 255;
  
  while (selectedAnswer == 255) {
    updateButtons();
    if (buttonPressed(1)) {
      selectedAnswer = 0; // Monday
      break;
    }
    if (buttonPressed(2)) {
      selectedAnswer = 1; // Tuesday
      break;
    }
    if (buttonPressed(3)) {
      selectedAnswer = 2; // Wednesday
      break;
    }
    delay(50);
  }
  
  uint16_t responseTime = millis() - questionStartTime;
  totalResponseTime += responseTime;
  responseCount++;
  
  if (selectedAnswer == correctDaySlot) {
    score++;
    ledCorrect();
    lcdClear();
    lcdSetCursor(4, 0);
    lcdPrint("Correct!");
    delay(1500);
  } else {
    ledIncorrect();
    lcdClear();
    lcdSetCursor(5, 0);
    lcdPrint("Oops!");
    delay(1500);
  }
  
  // Question 2: Time of day
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Time of day?");
  lcdSetCursor(0, 1);

  uint8_t periodOptions[3] = {0, 1, 2};  // morning, afternoon, evening
  uint8_t epochHour = now.tm_hour;
  uint8_t correctPeriod = 0;
  if (epochHour >= 5 && epochHour < 12) {
    correctPeriod = 0; // morning
  } else if (epochHour >= 12 && epochHour < 18) {
    correctPeriod = 1; // afternoon
  } else {
    correctPeriod = 2; // evening/night
  }
  rotateOptions(periodOptions, now.tm_min % 3);
  uint8_t correctPeriodSlot = 0;
  for (uint8_t i = 0; i < 3; i++) {
    if (periodOptions[i] == correctPeriod) {
      correctPeriodSlot = i;
      break;
    }
  }

  char timeLine[17];
  snprintf(timeLine, sizeof(timeLine), "A:%-3sB:%-3sC:%-3s",
           PERIOD_SHORT[periodOptions[0]],
           PERIOD_SHORT[periodOptions[1]],
           PERIOD_SHORT[periodOptions[2]]);
  lcdPrint(timeLine);
  delay(1000);
  
  questionStartTime = millis();
  selectedAnswer = 255;
  
  while (selectedAnswer == 255) {
    updateButtons();
    if (buttonPressed(1)) {
      selectedAnswer = 0;
      break;
    }
    if (buttonPressed(2)) {
      selectedAnswer = 1;
      break;
    }
    if (buttonPressed(3)) {
      selectedAnswer = 2;
      break;
    }
    delay(50);
  }
  
  responseTime = millis() - questionStartTime;
  totalResponseTime += responseTime;
  responseCount++;
  
  if (selectedAnswer == correctPeriodSlot) {
    score++;
    ledCorrect();
    lcdClear();
    lcdSetCursor(4, 0);
    lcdPrint("Correct!");
    delay(1500);
  } else {
    ledIncorrect();
    lcdClear();
    lcdSetCursor(5, 0);
    lcdPrint("Oops!");
    delay(1500);
  }
  
  // Question 3: Location
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Where are you?");
  lcdSetCursor(0, 1);
  lcdPrint("A:Hosp B:Home  ");  // Pad to 16 chars
  delay(1000);
  
  questionStartTime = millis();
  selectedAnswer = 255;
  
  while (selectedAnswer == 255) {
    updateButtons();
    if (buttonPressed(1)) {
      selectedAnswer = 0; // Hospital
      break;
    }
    if (buttonPressed(2)) {
      selectedAnswer = 1; // Home
      break;
    }
    delay(50);
  }
  
  responseTime = millis() - questionStartTime;
  totalResponseTime += responseTime;
  responseCount++;
  
  if (selectedAnswer == 0) {
    score++;
    ledCorrect();
    lcdClear();
    lcdSetCursor(4, 0);
    lcdPrint("Correct!");
    delay(1500);
  } else {
    ledIncorrect();
    lcdClear();
    lcdSetCursor(5, 0);
    lcdPrint("Oops!");
    delay(1500);
  }
  
  return score; // 0-3
}

uint8_t testMemory() {
  // Generate random sequence
  for (int i = 0; i < 3; i++) {
    memorySequence[i] = random(0, 3); // 0=A, 1=B, 2=C
  }
  
  // Show sequence
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Remember:");
  delay(1500);
  
  for (int i = 0; i < 3; i++) {
    lcdClear();
    lcdSetCursor(7, 0);
    lcdData('A' + memorySequence[i]);
    delay(800);
    lcdClear();
    delay(200);
  }
  
  // Get user input
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Repeat it:");
  lcdSetCursor(0, 1);
  lcdPrint("Press A/B/C   ");  // Pad to 16 chars
  delay(1000);
  
  questionStartTime = millis();
  memoryStep = 0;
  
  while (memoryStep < 3) {
    updateButtons();
    if (buttonPressed(1)) {
      userSequence[memoryStep] = 0;
      memoryStep++;
      lcdSetCursor(memoryStep - 1, 1);
      lcdData('A');
      delay(300);
    }
    if (buttonPressed(2)) {
      userSequence[memoryStep] = 1;
      memoryStep++;
      lcdSetCursor(memoryStep - 1, 1);
      lcdData('B');
      delay(300);
    }
    if (buttonPressed(3)) {
      userSequence[memoryStep] = 2;
      memoryStep++;
      lcdSetCursor(memoryStep - 1, 1);
      lcdData('C');
      delay(300);
    }
    delay(50);
  }
  
  uint16_t responseTime = millis() - questionStartTime;
  totalResponseTime += responseTime;
  responseCount++;
  
  // Check correctness
  uint8_t correct = 0;
  for (int i = 0; i < 3; i++) {
    if (memorySequence[i] == userSequence[i]) correct++;
  }
  
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Score:");
  lcdSetCursor(0, 1);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d/3", correct);
  lcdPrint(buf);
  delay(2000);
  
  if (correct == 3) {
    ledCorrect();
  } else {
    ledIncorrect();
  }
  
  return correct; // 0-3
}

uint8_t testAttention() {
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Press A when");
  lcdSetCursor(2, 1);
  lcdPrint("you see *");
  delay(2000);
  
  attentionTrials = 0;
  attentionCorrect = 0;
  
  for (int i = 0; i < 5; i++) {
    // Random wait
    delay(random(1500, 3500));
    
    // Show star
    lcdClear();
    lcdSetCursor(7, 0);
    lcdData('*');
    lcdSetCursor(7, 1);
    lcdData('*');
    
    attentionStartTime = millis();
    waitingForAttention = true;
    bool pressed = false;
    
    while (millis() - attentionStartTime < 2000) {
      updateButtons();
      if (buttonPressed(1)) { // Button A
        pressed = true;
        uint16_t reactionTime = millis() - attentionStartTime;
        totalResponseTime += reactionTime;
        responseCount++;
        attentionCorrect++;
        
        lcdClear();
        lcdSetCursor(0, 0);
        lcdPrint("Good!");
        lcdSetCursor(0, 1);
        lcdPrintNum(reactionTime);
        lcdPrint(" ms");
        delay(1000);
        break;
      }
      delay(10);
    }
    
    if (!pressed) {
      lcdClear();
      lcdSetCursor(5, 0);
      lcdPrint("Too slow!");
      delay(1000);
    }
    
    attentionTrials++;
  }
  
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Score:");
  lcdSetCursor(0, 1);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d/5", attentionCorrect);
  lcdPrint(buf);
  delay(2000);
  
  // Return 0-3 score
  if (attentionCorrect >= 4) {
    ledCorrect();
    return 3;
  } else if (attentionCorrect >= 3) {
    return 2;
  } else if (attentionCorrect >= 2) {
    return 1;
  } else {
    ledIncorrect();
    return 0;
  }
}

uint8_t testExecutiveFunction() {
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Order actions:");
  lcdSetCursor(0, 1);
  lcdPrint("A:Eat B:Shwr    ");  // Pad to 16 chars
  delay(2000);
  
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Select order:");
  lcdSetCursor(0, 1);
  lcdPrint("Press A/B/C    ");  // Pad to 16 chars
  
  sequenceStep = 0;
  uint8_t userSeq[4] = {255, 255, 255, 255};
  questionStartTime = millis();
  
  while (sequenceStep < 4) {
    updateButtons();
    if (buttonPressed(1)) {
      userSeq[sequenceStep] = 0;
      sequenceStep++;
      lcdSetCursor(sequenceStep * 2 - 2, 1);
      lcdData('A');
      delay(300);
    }
    if (buttonPressed(2)) {
      userSeq[sequenceStep] = 1;
      sequenceStep++;
      lcdSetCursor(sequenceStep * 2 - 2, 1);
      lcdData('B');
      delay(300);
    }
    if (buttonPressed(3)) {
      userSeq[sequenceStep] = 2;
      sequenceStep++;
      lcdSetCursor(sequenceStep * 2 - 2, 1);
      lcdData('C');
      delay(300);
    }
    delay(50);
  }
  
  uint16_t responseTime = millis() - questionStartTime;
  totalResponseTime += responseTime;
  responseCount++;
  
  // Check if sequence is correct (simplified check)
  uint8_t correct = 0;
  for (int i = 0; i < 4; i++) {
    if (userSeq[i] == correctSequence[i]) correct++;
  }
  
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Score:");
  lcdSetCursor(0, 1);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d/4", correct);
  lcdPrint(buf);
  delay(2000);
  
  if (correct == 4) {
    ledCorrect();
    return 3;
  } else if (correct >= 2) {
    return 2;
  } else {
    ledIncorrect();
    return 0;
  }
}

void runCognitiveAssessment() {
  lcdClear();
  lcdSetCursor(3, 0);
  lcdPrint("CogniPet");
  lcdSetCursor(0, 1);
  lcdPrint("Assessment...");
  lastR = 255; lastG = 255; lastB = 255; // Force update
  lcdSetRGB(0, 10, 30); // Very dim blue
  delay(2000);
  
  Serial.println("=== Starting Cognitive Assessment ===");

  ensureTimeSync();
  
  // Reset assessment
  memset(&lastAssessment, 0, sizeof(AssessmentResult));
  totalResponseTime = 0;
  responseCount = 0;
  
  // Test 1: Orientation
  lastAssessment.orientation_score = testOrientation();
  
  // Test 2: Memory
  lastAssessment.memory_score = testMemory();
  
  // Test 3: Attention
  lastAssessment.attention_score = testAttention();
  
  // Test 4: Executive function
  lastAssessment.executive_score = testExecutiveFunction();
  
  // Calculate totals
  lastAssessment.total_score = 
    lastAssessment.orientation_score +
    lastAssessment.memory_score +
    lastAssessment.attention_score +
    lastAssessment.executive_score;
  
  lastAssessment.avg_response_time_ms = totalResponseTime / responseCount;
  lastAssessment.timestamp = getCurrentTimestamp();
  
  // Determine alert level
  if (lastAssessment.total_score >= 10) {
    lastAssessment.alert_level = 0; // Green
  } else if (lastAssessment.total_score >= 7) {
    lastAssessment.alert_level = 1; // Yellow
  } else if (lastAssessment.total_score >= 4) {
    lastAssessment.alert_level = 2; // Orange
  } else {
    lastAssessment.alert_level = 3; // Red
  }
  
  // Show results
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Done! Score:");
  lcdSetCursor(0, 1);
  char scoreBuf[17];
  snprintf(scoreBuf, sizeof(scoreBuf), "%d/12", lastAssessment.total_score);
  lcdPrint(scoreBuf);
  delay(3000);
  
  // Send via BLE
  sendAssessmentViaBLE();
  
  // Transition to pet mode
  initializePet();
  currentState = STATE_PET_NORMAL;
  currentMenu = MENU_MAIN;  // Reset menu
  
  Serial.println("=== Assessment Complete ===");
  Serial.print("Total Score: ");
  Serial.print(lastAssessment.total_score);
  Serial.print("/12, Alert Level: ");
  Serial.println(lastAssessment.alert_level);
}

// ==== Virtual Pet Functions ====
void initializePet() {
  pet.happiness = 60;  // Start at neutral (not always green)
  pet.hunger = 50;     // Start a bit hungry
  pet.cleanliness = 70; // Start a bit dirty
  pet.lastFed = millis();
  pet.lastPlayed = millis();
  pet.lastCleaned = millis();
  
  // Reset RGB tracking to force update
  lastR = 255; lastG = 255; lastB = 255;
}

void updatePetStats() {
  unsigned long now = millis();
  if (now - pet.lastFed > 60000) { // Every minute
    pet.hunger = min(100, pet.hunger + 2);
    pet.cleanliness = max(0, pet.cleanliness - 1);
    
    // Intelligent happiness decay
    if (pet.hunger > 80) {
      pet.happiness = max(0, pet.happiness - 3);
    } else if (pet.hunger > 60) {
      pet.happiness = max(0, pet.happiness - 1);
    }
    
    if (pet.cleanliness < 20) {
      pet.happiness = max(0, pet.happiness - 2);
    }
    
    // Gradual happiness recovery if needs are met
    if (pet.hunger < 50 && pet.cleanliness > 50 && pet.happiness < 80) {
      pet.happiness = min(100, pet.happiness + 1);
    }
    
    pet.lastFed = now;
  }
}

void drawPetScreen() {
  static unsigned long lastDraw = 0;
  static uint8_t lastHappiness = 255;
  
  // Only redraw every 500ms to avoid flickering
  unsigned long now = millis();
  if (now - lastDraw < 500 && pet.happiness == lastHappiness && currentMenu == MENU_MAIN) {
    return;
  }
  lastDraw = now;
  lastHappiness = pet.happiness;
  
  lcdClear();
  
  if (currentMenu == MENU_MAIN) {
    // Line 1: Pet status (16 chars max)
    lcdSetCursor(0, 0);
    if (pet.happiness > 70) {
      lcdPrint("^_^ Buddy");
    } else if (pet.happiness > 40) {
      lcdPrint("-_- Buddy");
    } else {
      lcdPrint("T_T Buddy");
    }
    
    // Show status indicator (fit in remaining space)
    lcdSetCursor(10, 0);
    if (pet.hunger > 70) {
      lcdPrint("HUN");
    } else if (pet.cleanliness < 30) {
      lcdPrint("DRT");
    } else {
      lcdPrint("OK");
    }
    
    // Line 2: Menu (16 chars max) - Use all 3 buttons
    lcdSetCursor(0, 1);
    lcdPrint("A:Feed B:Play C:Cln");  // C: Clean (hold for menu)
    
  } else if (currentMenu == MENU_STATS) {
    // Stats screen
    lcdSetCursor(0, 0);
    lcdPrint("Stats:");
    lcdSetCursor(0, 1);
    char buf[17];
    snprintf(buf, sizeof(buf), "HP:%d%% H:%d%%", pet.happiness, pet.hunger);
    lcdPrint(buf);
    
  } else if (currentMenu == MENU_MOOD) {
    // Mood check screen
    lcdSetCursor(0, 0);
    lcdPrint("How are you?");
    lcdSetCursor(0, 1);
    lcdPrint("A:Good B:OK C:Sad");
    
  } else if (currentMenu == MENU_GAMES) {
    // Games menu
    lcdSetCursor(0, 0);
    lcdPrint("Choose game:");
    lcdSetCursor(0, 1);
    lcdPrint("A:Mem B:React C:Bk");
  }
  
  // Update RGB based on mood (only when happiness changes)
  // Use VERY dim colors so text is visible
  if (pet.happiness > 70) {
    lcdSetRGB(0, 20, 5); // Very dim green (happy)
  } else if (pet.happiness > 40) {
    lcdSetRGB(20, 20, 0); // Very dim yellow (neutral)
  } else {
    lcdSetRGB(20, 0, 0); // Very dim red (sad)
  }
}

void feedPet() {
  unsigned long startTime = millis();
  
  pet.hunger = max(0, pet.hunger - 30);
  pet.happiness = min(100, pet.happiness + 10);
  
  lcdClear();
  lcdSetCursor(3, 0);
  lcdPrint("Nom nom!");
  lcdSetCursor(4, 1);
  lcdPrint("Yummy!");
  
  // Flash green, then return to mood color
  lastR = 255; lastG = 255; lastB = 255; // Force update
  lcdSetRGB(0, 30, 0); // Dim green flash
  delay(1500);
  lastR = 255; lastG = 255; lastB = 255; // Force update to mood color
  
  logInteraction(0, millis() - startTime, 1, -1); // Type 0 = feed
}

void playWithPet() {
  // Mini-game: Reaction test
  unsigned long startTime = millis();
  
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Press A when");
  lcdSetCursor(0, 1);
  lcdPrint("you see *");
  delay(2000);
  
  // Random delay
  delay(random(1000, 2500));
  
  // Show star
  lcdClear();
  lcdSetCursor(7, 0);
  lcdData('*');
  lcdSetCursor(7, 1);
  lcdData('*');
  
  unsigned long showTime = millis();
  bool pressed = false;
  
  while (millis() - showTime < 2000) {
    updateButtons();
    if (buttonPressed(1)) { // Button A
      pressed = true;
      uint16_t reactionTime = millis() - showTime;
      
      pet.happiness = min(100, pet.happiness + 15);
      
      lcdClear();
      lcdSetCursor(4, 0);
      lcdPrint("Great!");
      lcdSetCursor(0, 1);
      char buf[17];
      snprintf(buf, sizeof(buf), "%d ms", reactionTime);
      lcdPrint(buf);
      lastR = 255; lastG = 255; lastB = 255; // Force update
      lcdSetRGB(0, 30, 0); // Dim green
      delay(1500);
      lastR = 255; lastG = 255; lastB = 255; // Force update to mood color
      
      logInteraction(1, reactionTime, 1, -1); // Type 1 = play
      break;
    }
    delay(10);
  }
  
  if (!pressed) {
    lcdClear();
    lcdSetCursor(3, 0);
    lcdPrint("Too slow!");
    lastR = 255; lastG = 255; lastB = 255; // Force update
    lcdSetRGB(30, 0, 0); // Dim red
    delay(1500);
    lastR = 255; lastG = 255; lastB = 255; // Force update to mood color
    
    logInteraction(1, 2000, 0, -1); // Failed
  }
}

void cleanPet() {
  unsigned long startTime = millis();
  
  pet.cleanliness = min(100, pet.cleanliness + 40);
  pet.happiness = min(100, pet.happiness + 5);
  
  lcdClear();
  lcdSetCursor(3, 0);
  lcdPrint("All clean!");
  lcdSetCursor(3, 1);
  lcdPrint("Thanks!");
  lastR = 255; lastG = 255; lastB = 255; // Force update
  lcdSetRGB(0, 20, 30); // Dim cyan
  delay(1500);
  lastR = 255; lastG = 255; lastB = 255; // Force update to mood color
  
  logInteraction(2, millis() - startTime, 1, -1); // Type 2 = clean
}

void showStats() {
  // Stats are shown in drawPetScreen, just handle navigation
  // This function is called when button is pressed in stats menu
}

void checkMood() {
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("How are you?");
  lcdSetCursor(0, 1);
  lcdPrint("A:Good B:OK C:Sad");
  
  unsigned long startTime = millis();
  int8_t mood = -1;
  
  while (millis() - startTime < 10000) { // 10 second timeout
    updateButtons();
    if (buttonPressed(1)) {
      mood = 0; // Good
      break;
    }
    if (buttonPressed(2)) {
      mood = 1; // OK
      break;
    }
    if (buttonPressed(3)) {
      mood = 2; // Sad
      break;
    }
    delay(50);
  }
  
  if (mood >= 0) {
    lcdClear();
    if (mood == 0) {
      lcdSetCursor(3, 0);
      lcdPrint("Great!");
      pet.happiness = min(100, pet.happiness + 5);
    } else if (mood == 1) {
      lcdSetCursor(5, 0);
      lcdPrint("OK");
    } else {
      lcdSetCursor(3, 0);
      lcdPrint("Sorry!");
      pet.happiness = max(0, pet.happiness - 5);
    }
    lcdSetCursor(0, 1);
    lcdPrint("Thanks!");
    
    logInteraction(3, millis() - startTime, 1, mood);
    delay(2000);
  }
  
  currentMenu = MENU_MAIN;
}

void playMemoryGame() {
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Memory Game!");
  lcdSetCursor(0, 1);
  lcdPrint("Remember seq ");  // Pad to 16
  delay(2000);
  
  // Generate sequence
  uint8_t seq[4];
  for (int i = 0; i < 4; i++) {
    seq[i] = random(1, 4); // 1, 2, or 3
  }
  
  // Show sequence
  for (int i = 0; i < 4; i++) {
    lcdClear();
    lcdSetCursor(6, 0);
    lcdData('A' + seq[i] - 1);
    delay(600);
    lcdClear();
    delay(200);
  }
  
  // Get input
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Repeat:");
  lcdSetCursor(0, 1);
  lcdPrint("Press A/B/C   ");  // Pad to 16
  
  uint8_t userSeq[4] = {0};
  unsigned long startTime = millis();
  
  for (int i = 0; i < 4; i++) {
    while (millis() - startTime < 15000) {
      updateButtons();
      if (buttonPressed(1)) {
        userSeq[i] = 1;
        lcdSetCursor(i * 2, 1);
        lcdData('A');
        delay(300);
        break;
      }
      if (buttonPressed(2)) {
        userSeq[i] = 2;
        lcdSetCursor(i * 2, 1);
        lcdData('B');
        delay(300);
        break;
      }
      if (buttonPressed(3)) {
        userSeq[i] = 3;
        lcdSetCursor(i * 2, 1);
        lcdData('C');
        delay(300);
        break;
      }
      delay(50);
    }
  }
  
  // Check
  uint8_t correct = 0;
  for (int i = 0; i < 4; i++) {
    if (seq[i] == userSeq[i]) correct++;
  }
  
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Score:");
  lcdSetCursor(0, 1);
  char buf[17];
  snprintf(buf, sizeof(buf), "%d/4", correct);
  lcdPrint(buf);
  
  if (correct == 4) {
    pet.happiness = min(100, pet.happiness + 10);
    ledCorrect();
    lcdSetCursor(7, 0);
    lcdPrint("Perfect!");
  } else if (correct >= 2) {
    lcdSetCursor(7, 0);
    lcdPrint("Good!");
  } else {
    ledIncorrect();
  }
  
  logInteraction(4, millis() - startTime, correct == 4 ? 1 : 0, -1);
  delay(2500);
  currentMenu = MENU_MAIN;
}

bool checkTestDataBackdoor() {
  // Test Data Backdoor: Hold Button 1 + Button 3 together for 2 seconds to send test assessment
  static unsigned long testDataStart = 0;
  static bool testDataActive = false;
  static bool testDataShown = false;
  
  bool btn1 = (digitalRead(BTN1_PIN) == LOW);
  bool btn3 = (digitalRead(BTN3_PIN) == LOW);
  
  if (btn1 && btn3 && !testDataActive) {
    // Both buttons just pressed
    testDataStart = millis();
    testDataActive = true;
    testDataShown = false;
  } else if (btn1 && btn3 && testDataActive) {
    // Still holding both
    unsigned long elapsed = millis() - testDataStart;
    
    // Show countdown after 1 second
    if (elapsed > 1000 && !testDataShown) {
      lcdClear();
      lcdSetCursor(0, 0);
      lcdPrint("Test Data...");
      lcdSetCursor(0, 1);
      lcdPrint("Hold 2 sec...");
      testDataShown = true;
    }
    
    if (elapsed > 2000) {
      // 2 seconds held - send test data
      testDataActive = false;
      return true;
    }
  } else {
    // Buttons released or not both pressed
    testDataActive = false;
  }
  
  return false;
}

void sendTestAssessmentData() {
  // Create test assessment data with varying scores
  // Cycles through different test scenarios
  static uint8_t testScenario = 0;
  
  // Clear last assessment
  memset(&lastAssessment, 0, sizeof(AssessmentResult));
  
  // Different test scenarios
  switch (testScenario % 4) {
    case 0: // Excellent assessment
      lastAssessment.orientation_score = 3;
      lastAssessment.memory_score = 3;
      lastAssessment.attention_score = 3;
      lastAssessment.executive_score = 3;
      lastAssessment.avg_response_time_ms = 2000;
      break;
    case 1: // Moderate assessment
      lastAssessment.orientation_score = 2;
      lastAssessment.memory_score = 2;
      lastAssessment.attention_score = 2;
      lastAssessment.executive_score = 2;
      lastAssessment.avg_response_time_ms = 3000;
      break;
    case 2: // Poor assessment
      lastAssessment.orientation_score = 1;
      lastAssessment.memory_score = 1;
      lastAssessment.attention_score = 1;
      lastAssessment.executive_score = 1;
      lastAssessment.avg_response_time_ms = 5000;
      break;
    case 3: // Mixed assessment
      lastAssessment.orientation_score = 3;
      lastAssessment.memory_score = 1;
      lastAssessment.attention_score = 2;
      lastAssessment.executive_score = 2;
      lastAssessment.avg_response_time_ms = 3500;
      break;
  }
  
  // Calculate totals
  lastAssessment.total_score = 
    lastAssessment.orientation_score +
    lastAssessment.memory_score +
    lastAssessment.attention_score +
    lastAssessment.executive_score;
  
  lastAssessment.timestamp = getCurrentTimestamp();
  
  // Determine alert level
  if (lastAssessment.total_score >= 10) {
    lastAssessment.alert_level = 0; // Green
  } else if (lastAssessment.total_score >= 7) {
    lastAssessment.alert_level = 1; // Yellow
  } else if (lastAssessment.total_score >= 4) {
    lastAssessment.alert_level = 2; // Orange
  } else {
    lastAssessment.alert_level = 3; // Red
  }
  
  // Show on LCD
  lcdClear();
  lcdSetCursor(0, 0);
  lcdPrint("Test Data Sent!");
  lcdSetCursor(0, 1);
  char scoreBuf[17];
  snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d/12", lastAssessment.total_score);
  lcdPrint(scoreBuf);
  
  // Send via BLE
  sendAssessmentViaBLE();
  
  // Also send a test interaction
  logInteraction(0, 500, true, -1); // Feed interaction
  
  Serial.print("Test assessment sent: Score ");
  Serial.print(lastAssessment.total_score);
  Serial.print("/12, Alert Level ");
  Serial.println(lastAssessment.alert_level);
  
  // Cycle to next scenario
  testScenario++;
  
  delay(2000);
}

bool checkBackdoor() {
  // Backdoor: Hold Button 1 + Button 2 together for 2 seconds to trigger assessment
  static unsigned long backdoorStart = 0;
  static bool backdoorActive = false;
  static bool backdoorShown = false;
  
  bool btn1 = (digitalRead(BTN1_PIN) == LOW);
  bool btn2 = (digitalRead(BTN2_PIN) == LOW);
  
  if (btn1 && btn2 && !backdoorActive) {
    // Both buttons just pressed
    backdoorStart = millis();
    backdoorActive = true;
    backdoorShown = false;
  } else if (btn1 && btn2 && backdoorActive) {
    // Still holding both
    unsigned long elapsed = millis() - backdoorStart;
    
    // Show countdown after 1 second
    if (elapsed > 1000 && !backdoorShown) {
      lcdClear();
      lcdSetCursor(0, 0);
      lcdPrint("Assessment...");
      lcdSetCursor(0, 1);
      lcdPrint("Hold 2 sec...");
      backdoorShown = true;
    }
    
    if (elapsed > 2000) {
      // 2 seconds held - trigger assessment
      backdoorActive = false;
      backdoorShown = false;
      return true;
    }
  } else {
    // Not both pressed
    if (backdoorActive && backdoorShown) {
      // Was showing backdoor, clear it
      lcdClear();
    }
    backdoorActive = false;
    backdoorShown = false;
  }
  
  return false;
}

bool checkDiagnosticsBackdoor() {
  static bool comboActive = false;
  static unsigned long comboStart = 0;
  static bool hintShown = false;

  bool btn2 = (digitalRead(BTN2_PIN) == LOW);
  bool btn3 = (digitalRead(BTN3_PIN) == LOW);

  if (btn2 && btn3 && !comboActive) {
    comboActive = true;
    comboStart = millis();
    hintShown = false;
  } else if (btn2 && btn3 && comboActive) {
    unsigned long elapsed = millis() - comboStart;
    if (elapsed > 1000 && !hintShown) {
      lcdClear();
      lcdSetCursor(0, 0);
      lcdPrint("Diagnostics...");
      lcdSetCursor(0, 1);
      lcdPrint("Hold 2 sec...");
      hintShown = true;
    }
    if (elapsed > 2000) {
      comboActive = false;
      hintShown = false;
      return true;
    }
  } else {
    if (comboActive && hintShown) {
      lcdClear();
    }
    comboActive = false;
    hintShown = false;
  }

  return false;
}

void runDiagnosticsMode() {
  updateButtons();
  unsigned long now = millis();

  if (buttonPressed(1)) {
    diagnosticsPage = (diagnosticsPage + 1) % DIAGNOSTIC_PAGE_COUNT;
    lastDiagnosticsRefresh = 0;
  } else if (buttonPressed(2)) {
    diagnosticsPage = (diagnosticsPage + DIAGNOSTIC_PAGE_COUNT - 1) % DIAGNOSTIC_PAGE_COUNT;
    lastDiagnosticsRefresh = 0;
  }

  static bool exitHold = false;
  static unsigned long exitStart = 0;
  bool btn3 = (digitalRead(BTN3_PIN) == LOW);
  if (btn3 && !exitHold) {
    exitHold = true;
    exitStart = now;
  } else if (btn3 && exitHold) {
    if (now - exitStart > 1500) {
      exitHold = false;
      diagnosticsActive = false;
      lcdClear();
      currentState = STATE_PET_NORMAL;
      return;
    }
  } else if (!btn3) {
    exitHold = false;
  }

  if (now - lastDiagnosticsRefresh < 400) {
    return;
  }
  lastDiagnosticsRefresh = now;

  lcdClear();
  char line[17];

  switch (diagnosticsPage) {
    case 0: {
      if (wifiConnectedFlag) {
        snprintf(line, sizeof(line), "WiFi:ON %s", lastWifiIp);
      } else if (wifiEverConnected) {
        snprintf(line, sizeof(line), "WiFi:LAST %s", lastWifiIp);
      } else {
        snprintf(line, sizeof(line), "WiFi:OFF --");
      }
      lcdSetCursor(0, 0);
      lcdPrintPadded(line, 16);

      if (timeSynced && lastNtpSyncMs > 0) {
        unsigned long mins = (now - lastNtpSyncMs) / 60000UL;
        if (mins > 99) mins = 99;
        snprintf(line, sizeof(line), "Sync:%2lum ago", mins);
      } else if (hasWiFiCredentials()) {
        snprintf(line, sizeof(line), "Sync:pending...");
      } else {
        snprintf(line, sizeof(line), "Sync:disabled ");
      }
      lcdSetCursor(0, 1);
      lcdPrintPadded(line, 16);
      break;
    }
    case 1: {
      uint8_t queueCount = (queueTail + 20 - queueHead) % 20;
      snprintf(line, sizeof(line), "BLE:%s Q:%02u", deviceConnected ? "LINK" : "SCAN", queueCount);
      lcdSetCursor(0, 0);
      lcdPrintPadded(line, 16);

      snprintf(line, sizeof(line), "Alert:%d Score:%02d", lastAssessment.alert_level, lastAssessment.total_score);
      lcdSetCursor(0, 1);
      lcdPrintPadded(line, 16);
      break;
    }
    case 2: {
      bool b1 = (digitalRead(BTN1_PIN) == LOW);
      bool b2 = (digitalRead(BTN2_PIN) == LOW);
      bool b3 = (digitalRead(BTN3_PIN) == LOW);
      snprintf(line, sizeof(line), "Btn1:%d Btn2:%d", b1, b2);
      lcdSetCursor(0, 0);
      lcdPrintPadded(line, 16);

      snprintf(line, sizeof(line), "Btn3:%d hold exit", b3);
      lcdSetCursor(0, 1);
      lcdPrintPadded(line, 16);
      break;
    }
    case 3: {
      snprintf(line, sizeof(line), "Mood:%3d Hun:%3d", pet.happiness, pet.hunger);
      lcdSetCursor(0, 0);
      lcdPrintPadded(line, 16);
      snprintf(line, sizeof(line), "Clean:%3d Risk:%d", pet.cleanliness, lastAssessment.alert_level);
      lcdSetCursor(0, 1);
      lcdPrintPadded(line, 16);
      break;
    }
  }
}

void handlePetInput() {
  static unsigned long btn3HoldStart = 0;
  static bool btn3Holding = false;
  
  updateButtons();
  
  if (currentMenu == MENU_MAIN) {
    if (buttonPressed(1)) { // Feed
      feedPet();
    } else if (buttonPressed(2)) { // Play
      playWithPet();
    } else if (btn3Pressed && !btn3Last) { // Button 3 just pressed
      btn3HoldStart = millis();
      btn3Holding = true;
    } else if (!btn3Pressed && btn3Last && btn3Holding) {
      // Button 3 released
      unsigned long pressDuration = millis() - btn3HoldStart;
      if (pressDuration > 1000) {
        // Long press: Open menu
        currentMenu = MENU_STATS;
        lastMenuChange = millis();
      } else if (pressDuration > 50) {
        // Short press: Clean
        cleanPet();
      }
      btn3Holding = false;
    } else if (btn3Pressed && btn3Holding && (millis() - btn3HoldStart > 1000)) {
      // Still holding after 1 second - show menu hint
      lcdClear();
      lcdSetCursor(0, 0);
      lcdPrint("Menu...");
      delay(100);
    }
  } else if (currentMenu == MENU_STATS) {
    if (buttonPressed(1)) { // Next: Mood
      currentMenu = MENU_MOOD;
      lastMenuChange = millis();
    } else if (buttonPressed(2)) { // Next: Games
      currentMenu = MENU_GAMES;
      lastMenuChange = millis();
    } else if (buttonPressed(3)) { // Back to main
      currentMenu = MENU_MAIN;
    }
  } else if (currentMenu == MENU_MOOD) {
    if (buttonPressed(1) || buttonPressed(2) || buttonPressed(3)) {
      checkMood();
    }
  } else if (currentMenu == MENU_GAMES) {
    if (buttonPressed(1)) { // Memory game
      playMemoryGame();
      return;
    } else if (buttonPressed(2)) { // Reaction game
      playWithPet();
      return;
    } else if (buttonPressed(3)) { // Back
      currentMenu = MENU_MAIN;
    }
  }
  
  // Auto-return to main menu after 10 seconds in sub-menus
  if (currentMenu != MENU_MAIN && millis() - lastMenuChange > 10000) {
    currentMenu = MENU_MAIN;
  }
}

// ==== Main Setup ====
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BTN3_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(1000);  // Give Serial time to initialize
  Serial.println("\n\n=== CogniPet Starting ===");
  Serial.println("Serial initialized");

  // ESP32-S3 I2C setup - try with frequency specification
  Serial.print("Initializing I2C on SDA=");
  Serial.print(SDA_PIN);
  Serial.print(" SCL=");
  Serial.println(SCL_PIN);
  
  bool i2cStarted = Wire.begin(SDA_PIN, SCL_PIN, 100000); // 100kHz I2C speed
  if (!i2cStarted) {
    Serial.println("ERROR: I2C begin failed!");
  } else {
    Serial.println("I2C initialized successfully");
  }
  delay(200);
  
  Serial.println("Starting I2C scan...");
  i2cScan();
  
  // Try to turn off RGB immediately (before LCD init)
  Serial.println("Attempting to turn off RGB backlight...");
  lastR = 255; lastG = 255; lastB = 255; // Force update
  lcdSetRGB(0, 0, 0); // Turn OFF RGB
  delay(200);
  
  // Test LCD communication
  Serial.println("Testing LCD communication...");
  Wire.beginTransmission(LCD_ADDR);
  uint8_t lcdError = Wire.endTransmission();
  if (lcdError == 0) {
    Serial.println("LCD responds to I2C");
  } else {
    Serial.print("LCD I2C error: ");
    Serial.println(lcdError);
  }

  lcdInit();
  
  // Set RGB to very dim after LCD is initialized
  delay(200);
  lastR = 255; lastG = 255; lastB = 255; // Force update
  lcdSetRGB(0, 0, 0); // Keep it OFF initially
  delay(300);
  lcdSetRGB(0, 5, 10); // Very dim blue for startup
  
  // Check if first boot
  if (isFirstBoot()) {
    currentState = STATE_ASSESSMENT;
    lcdClear();
    lcdSetCursor(3, 0);
    lcdPrint("Welcome!");
  lcdSetCursor(0, 1);
    lcdPrint("First setup");
    delay(2000);
  } else {
    currentState = STATE_PET_NORMAL;
    initializePet();
  }
  
  // Initialize BLE
  Serial.println("Initializing BLE...");
  setupBLE();
  Serial.println("BLE setup complete");

  initializeTimeService();
  
  Serial.println("=== CogniPet initialized successfully ===");
  Serial.println("Ready for use!");
  Serial.println("BLE Device Name: CogniPet");
  Serial.println("Waiting for BLE connection...");
}

// ==== Main Loop ====
void loop() {
  maintainTimeService();
  // Handle BLE connection
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // Check backdoor from any state (global backdoor check)
  if (checkBackdoor()) {
    lcdClear();
    lcdSetCursor(2, 0);
    lcdPrint("Backdoor!");
  lcdSetCursor(0, 1);
    lcdPrint("Assessment...");
    delay(1500);
    currentState = STATE_ASSESSMENT;
    currentMenu = MENU_MAIN;
  }
  
  // Check test data backdoor from any state (global test data check)
  if (checkTestDataBackdoor()) {
    sendTestAssessmentData();
  }

  if (currentState != STATE_DIAGNOSTICS && checkDiagnosticsBackdoor()) {
    diagnosticsActive = true;
    diagnosticsPage = 0;
    lastDiagnosticsRefresh = 0;
    lcdClear();
    currentState = STATE_DIAGNOSTICS;
  }
  
  switch(currentState) {
    case STATE_ASSESSMENT:
      runCognitiveAssessment();
      markBootComplete();
      break;
      
    case STATE_PET_NORMAL:
      updatePetStats();
      handlePetInput();
      
      // Only draw screen if still in pet mode (backdoor might have changed state)
      if (currentState == STATE_PET_NORMAL) {
        drawPetScreen();
      }
      delay(100);
      break;

    case STATE_DIAGNOSTICS:
      runDiagnosticsMode();
      break;
      
    default:
      currentState = STATE_PET_NORMAL;
      break;
  }
  
  delay(50);
}
