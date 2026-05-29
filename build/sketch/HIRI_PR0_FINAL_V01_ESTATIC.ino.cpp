#include <Arduino.h>
#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
/*
 * FirmwarePro.ino - Version Pro V0.1.23V
 * Firmware HIRI PR0 estatico: sensores, GNSS, SD, UI OLED y telemetria HTTP.
 * 
 * Basado en la version Debug 0.1.3V (la mas estable) pero con mejoras de performance:
 * - Modem inicializado de forma sincronica en boot para maxima estabilidad.
 * - Lectura de sensores Serial (SDS198 / PMS) ASINCRONICA (no bloqueante).
 * - I2C con reloj conservador y protecciones contra cuelgues.
 * - Monitoreo de etapas (setStage) y Heartbeat (RAM/Heap).
 */

#include "config.h"
#include "Adafruit_SHT31.h"
#include "Adafruit_SHT4x.h"
#include "DFRobot_MultiGasSensor.h"
#include <DFRobot_ENS160.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <Preferences.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <DNSServer.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_task_wdt.h>

// Modem
#define SerialAT Serial1
TinyGsm modem(SerialAT);

// -------------------- Global Constants --------------------
#define RTC_PROBE_PERIOD_MS 60000
#define RTC_SYNC_THRESHOLD 30
#define MIN_VALID_EPOCH 1672531200 // 2023-01-01
#define WDT_TIMEOUT 60
static const uint32_t I2C_CLOCK_HZ = 50000;
static const uint32_t HEARTBEAT_PERIOD_MS = 10000;
static const uint32_t MODEM_TESTAT_TOTAL_MS = 45000;
static const uint32_t MODEM_TESTAT_RETRY_MS = 1000;

const byte HEADER = 0xAA;
const byte CMD = 0xCF;
const byte TAIL = 0xAB;

String VERSION = "Pro V0.1.23V";

// -------------------- Global States --------------------
bool oledOK;
bool rtcOK = false;
bool SHT31OK = false;
bool SHT4xOK = false;
bool SDS198OK = false;
bool GasOK = false;
bool ENS160OK = false;
bool SDOK = false;
bool wifiModeActive = false;
bool hasRed = false;
bool wdtStarted = false;
const char *lastStage = "BOOT";

SystemConfig config;

// Hardware Objects
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
Adafruit_SHT4x sht4 = Adafruit_SHT4x();
DFRobot_GAS_I2C gas(&Wire, 0x74);
DFRobot_ENS160_I2C ENS160(&Wire, 0x53);
RTC_DS3231 rtc;
Preferences prefs;
SPIClass spiSD(HSPI);
WebServer server(80);
DNSServer dnsServer;

SoftwareSerial pms(pms_TX, pms_RX);

// Button flags (edge + debounce)
volatile bool btn1ClickFlag = false;
volatile bool btn2ClickFlag = false;
volatile bool btn2HoldFlag = false;
volatile uint32_t lastDebounceTime1 = 0;
volatile uint32_t lastDebounceTime2 = 0;
const uint32_t BTN1_DEBOUNCE_MS = 80;
const uint32_t BTN2_DEBOUNCE_MS = 80;

// Global Data
uint16_t PM1 = 0, PM25 = 0, PM10 = 0;
float pmsTempC = NAN, pmsHum = NAN;
int SDS198PM100 = 0;
float tempsht31 = NAN, humsht31 = NAN;
float tempsht4x = NAN, humsht4x = NAN;
float rtcTempC = NAN;
float batV = 0.0f;
int csq = 0;
bool networkError = false;
bool streaming = false;
bool loggingEnabled = false;

String gpsLat = "NaN", gpsLon = "NaN";
String gpsTime = "N/A", gpsDate = "N/A";
String satellitesStr = "0", hdopStr = "N/A", gpsAlt = "N/A";
String gpsStatus = "NoFix";
String gpsSpeedKmh = "0.0";

uint8_t debugScreenIndex = 0;
uint32_t lastDebugRotationMs = 0;
const uint32_t DEBUG_ROTATION_INTERVAL_MS = 10000;

String csvFileName = "";
String logFilePath = "";
String failedTxPath = "";
String currentNote = "9";
String lastSavedCSVLine = "";
File uploadFile;
String deviceID = "/HIRIPV";
const char *DEVICE_ID_STR = "1"; // Se actualizara desde config o manualmente
String AP_SSID_STR = "";
const char *AP_PASSWORD = "12345678";
String apIpStr = "0.0.0.0";

const char apn[] = "flolive.net";
const char gprsUser[] = "";
const char gprsPass[] = "";
const char *API_BASE = "http://api-sensores.cmasccp.cl/insertarMedicion";
const char *GLOBAL_IDS_VARIABLES = "53,54,55,11,12,15,45,46,4,3,6,7,8,9,51,3,6";

uint32_t sendCounter = 0;
uint32_t sdSaveCounter = 0;
uint32_t lastHttpSend = 0;
uint32_t lastSdSave = 0;
uint32_t lastHttpActivityMs = 0;
uint32_t lastSdActivityMs = 0;
bool lastHttpOk = false;
bool lastSdOk = false;
bool hasHttpAttempted = false;
bool bootHttpAttemptPending = true;
uint8_t lastDayLogged = 0;
bool wasStreamingBeforeBoot = false;

// Animation Variables
int logoXOffset = -25;
int hiriXOffset = 128;
int proYOffset = 64;
const int LOGO_FINAL_X = 4;
const int HIRI_FINAL_X = 48;
const int PRO_FINAL_X = 106;
const int HIRI_FINAL_Y = 44;
const int PRO_FINAL_Y = 52;

// Watchdog, network and diagnostics
String rebootReason = "Unknown";
String networkOperator = "N/A";
String networkTech = "N/A";
String signalQuality = "0";
String registrationStatus = "N/A";
uint32_t lastXtraDownload = 0;
bool xtraSupported = false;
bool xtraLastOk = false;
const uint32_t XTRA_REFRESH_MS = 3UL * 24UL * 60UL * 60UL * 1000UL;
const uint8_t ENS160_INVALID_REINIT_THRESHOLD = 3;
const uint8_t SHT4X_FAIL_REINIT_THRESHOLD = 3;
const uint8_t HTTP_FAIL_MAX_CONSECUTIVE = 6;
const uint32_t HTTP_RETRY_BACKOFF_MS = 60UL * 60UL * 1000UL;
uint8_t httpConsecutiveFailCount = 0;
bool httpBackoffActive = false;
uint32_t httpBackoffUntilMs = 0;
uint8_t ens160InvalidCount = 0;
uint8_t sht4xFailCount = 0;
uint8_t ens160StatusRaw = 0;
uint16_t ens160Tvoc = 0;
uint16_t ens160Eco2 = 0;
uint8_t ens160Aqi = 0;
bool ens160DataValid = false;
char currentCriticalStage[32] = "boot";

// Display State
volatile DisplayState displayState = DISP_NORMAL;
volatile uint32_t displayStateStartTime = 0;
uint32_t lastOledActivity = 0;

// Modem Sync
uint8_t rtcModemSyncCount = 0;
uint32_t lastModemSyncAttempt = 0;
const uint32_t MODEM_SYNC_INTERVAL_MS = 600000;
const uint8_t MAX_MODEM_SYNC_COUNT = 3;
bool rtcNetSyncPending = false;
uint32_t rtcNextProbeMs = 0;

// AT Command Struct
struct AtSession {
  bool active = false;
  String expect1;
  String expect2;
  String resp;
  uint32_t deadline = 0;
} at;

// Buffer for PMS
static uint8_t pmsBuf[64];
static size_t pmsHead = 0;
static uint32_t lastPmsSeen = 0;

// Battery Averaging
const float alpha = 0.8;
float batteryVoltageAverage = 0;
static uint32_t lastBatSample = 0;
static float batSampleSum = 0;
static int batSampleCount = 0;
const int NUM_SAMPLES = 30;
const uint32_t BAT_SAMPLE_INTERVAL_MS = 5;

// Variables needed for GPS diag
uint32_t lastNmeaSeenMs = 0;
uint32_t gnssFixFirstMs = 0;
bool gnssFixReported = false;
uint8_t gsaFixType = 0;
uint8_t gsaSatsUsed = 0;
float gsaPdop = NAN, gsaHdop2 = NAN, gsaVdop = NAN;
uint16_t gsvSatsInView = 0;
float gsvSnrAvg = 0, gsvSnrMax = 0;
uint32_t gsvLastMs = 0;
float gsvSnrAcc = 0;
int gsvSnrCnt = 0;
uint32_t lastNmeaMs = 0;
uint32_t lastGgaMs = 0;
uint32_t lastFixMs = 0;
uint16_t nmeaCount1s = 0;
uint16_t nmeaRate = 0;
uint32_t nmeaRefMs = 0;
uint8_t fixQLast = 0;
bool ttffPrinted = false;
uint32_t gnssStartMs = 0;
bool haveFix = false;

// SD Definition constants
const int SD_SCLK = 14, SD_MISO = 2, SD_MOSI = 15, SD_CS = 13;

// -------------------- Prototypes --------------------
void loadConfig();
void saveConfig();
void applyLEDConfig();
void writeErrorLogHeader();
String generateCSVFileName();
void writeCSVHeader();
void drawAnimation();
void startWifiApServer();
void stopWifiApServer();
void renderDisplay();
bool saveCSVData();
void checkRebootReason();
void readPMS();
bool readFrameSDS198(byte *buf); // Ahora asincronico
void updatePmLed(float pm25);
void gnssBringUp();
void gnssDiagTick();
void gnssDebugPollAsync();
void gnssWatchdog();
bool atTick(bool &done, bool &ok);
bool atRun(const String &cmd, const String &expect1 = "OK", const String &expect2 = "ERROR", uint32_t timeout_ms = 8000);
bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms = 4000);
bool httpGet_webhook(const String &url);
bool detectAndEnableXtra();
bool downloadXtraOnce();
void parseNMEA(const String &line);
void saveFailedTransmission(const String &url, const String &errorType);
bool sendCurrentMeasurement();
void handleButtonLogic();
void showMessage(const char *msg);
void processSerialCommand();
void logError(const String &type, const String &ctx, const String &msg);
void IRAM_ATTR isr_btn2();
void initI2CSensors();
void recoverI2CBus(const char *reason);
void updateEns160State();
extern void ui_btn1_click();
extern void ui_btn2_click();
extern bool uiFullMode;
String missingUrlValue();
String safeFloatStr(float v);
String safeUIntStr(uint32_t v);
String safeIntStr(int v);
String safeNumberStr(const String &s);
String safeGpsStr(const String &s);
String safeSatsStr(const String &s);

// -------------------- Debug Helpers --------------------
#line 291 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void feedWdt();
#line 292 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void startWdtOnce();
#line 300 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void setStage(const char *stage);
#line 302 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void printHeartbeat();
#line 310 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void configureI2CBus();
#line 402 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void enforceStationStartupConfig();
#line 430 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void atBegin(const String &cmd, const String &expect1, const String &expect2, uint32_t timeout_ms);
#line 476 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void updateNetworkInfo();
#line 493 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
String getIdsSensores(const String& deviceId);
#line 540 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void refreshSensors2s();
#line 569 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
bool waitForModemAT();
#line 615 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void setup();
#line 716 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void loop();
#line 74 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\PMSandSensorEXTRA.ino"
static uint8_t lerp8(uint8_t a, uint8_t b, float t);
#line 113 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\PMSandSensorEXTRA.ino"
bool readFrameSDS198(byte* outFrame);
#line 42 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void splitSentence(const String &sentence, char delimiter, String fields[], int expectedFields);
#line 56 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
float convertToDecimal(String coord, String dir);
#line 71 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
static void resetGnssFlagsAfterStart();
#line 83 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void parseGSA(const String &s);
#line 103 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void parseGSV(const String &s);
#line 139 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void parseGGA(const String &s);
#line 170 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void parseRMC(const String &s);
#line 196 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void parseVTG(const String &s);
#line 232 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
bool setGnssAllWithFallback();
#line 321 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void gnssHotRestart();
#line 331 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void gnssWarmRestart();
#line 505 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
void downloadXtraIfDue();
#line 32 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\http.ino"
bool ensurePdpAndNet();
#line 109 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\http.ino"
void parseHttpActionResponse(const String &resp, int &code, int &dataLen);
#line 124 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\http.ino"
bool httpGet_webhook(const String &fullUrl);
#line 3 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\rtc.ino"
void printRtcOnce();
#line 11 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\rtc.ino"
static uint8_t monthFromAbbrev(const char *m);
#line 42 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\rtc.ino"
static uint32_t compileUnixTime();
#line 66 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\rtc.ino"
bool getModemEpoch(uint32_t &epoch_out);
#line 109 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\rtc.ino"
void syncRtcSmart();
#line 89 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawWifiModeScreen();
#line 114 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void toggleSamplingAction();
#line 185 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void showMessageFor(const char *msg, uint32_t durationMs);
#line 263 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
static bool uiCanHandleAction();
#line 279 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
String getClockTime();
#line 291 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
int calcBatteryPercent(float v);
#line 301 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawBatteryDynamic(int xPos, int yPos, float v);
#line 346 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawActivityDot(int x, bool enabled, bool active, bool ok);
#line 363 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawHeader();
#line 423 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawFooterCircles(uint8_t cnt, uint8_t sel);
#line 438 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawSensorValue(uint8_t idx);
#line 469 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawMenuItemWithIcon(uint8_t depth, uint8_t idx);
#line 488 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawFullModeView();
#line 695 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawNetworkInfo();
#line 720 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawRtcInfo();
#line 747 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawStorageInfo();
#line 770 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void drawGpsInfo();
#line 791 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void handleRestart();
#line 808 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void handleConfigWifi();
#line 1044 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
void updateDisplayStateMachine();
#line 181 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
static String sanitizeForId(const String &s);
#line 192 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
static bool isSafeFilename(const String &name);
#line 201 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
static String normalizeSdPath(String name);
#line 207 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
static bool ensureWifiAuth();
#line 213 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void listFiles();
#line 259 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void handleFileDownload();
#line 287 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void handleFileDelete();
#line 305 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void handleFileRename();
#line 326 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void handleFileUpload();
#line 358 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void handleDeleteAll();
#line 394 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void handleIp();
#line 396 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void handleSsid();
#line 398 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
void setupWifiRoutes();
#line 291 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\HIRI_PR0_FINAL_V01_ESTATIC.ino"
void feedWdt() { if (wdtStarted) esp_task_wdt_reset(); }
void startWdtOnce() {
  if (!wdtStarted) {
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    wdtStarted = true;
    Serial.println("[WDT] Started");
  }
}
void setStage(const char *stage) { lastStage = stage; }

void printHeartbeat() {
  static uint32_t lastHeartbeatMs = 0;
  if (millis() - lastHeartbeatMs < HEARTBEAT_PERIOD_MS) return;
  lastHeartbeatMs = millis();
  Serial.printf("[HB] ms=%lu stage=%s heap=%u sd=%d stream=%d batV=%.3f\n", 
                millis(), lastStage, ESP.getFreeHeap(), SDOK, streaming, batV);
}

void configureI2CBus() {
  setStage("i2c.begin");
  Wire.begin();
  Wire.setTimeOut(50);
  Wire.setClock(I2C_CLOCK_HZ);
  delay(50);
  Serial.printf("[I2C] Clock: %u Hz\n", I2C_CLOCK_HZ);
}

void initI2CSensors() {
  setStage("i2c.sensors.init");

  if (!sht4.begin()) {
    SHT4xOK = false;
    logError("I2C_SENSOR_FAIL", "SHT4X.begin", "SHT4x not found");
  } else {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    SHT4xOK = true;
    sht4xFailCount = 0;
    Serial.println("[SHT4X] OK");
  }

  if (!sht31.begin(0x44)) {
    SHT31OK = false;
    logError("I2C_SENSOR_FAIL", "SHT31.begin", "SHT31 not found");
  } else {
    SHT31OK = true;
    Serial.println("[SHT31] OK");
  }

  if (NO_ERR != ENS160.begin()) {
    ENS160OK = false;
    ens160DataValid = false;
    logError("I2C_SENSOR_FAIL", "ENS160.begin", "ENS160 init failed");
  } else {
    ENS160.setPWRMode(ENS160_STANDARD_MODE);
    ENS160OK = true;
    ens160InvalidCount = 0;
    ens160DataValid = false;
    Serial.println("[ENS160] OK");
  }
}

void recoverI2CBus(const char *reason) {
  logError("I2C_RECOVER", "recoverI2CBus", reason);
  setStage("i2c.recover");
  Wire.end();
  delay(50);
  configureI2CBus();
  initI2CSensors();
}

void updateEns160State() {
  if (!ENS160OK) {
    ens160DataValid = false;
    return;
  }

  setStage("ens160.read");
  float tc = !isnan(tempsht4x) ? tempsht4x : (!isnan(tempsht31) ? tempsht31 : pmsTempC);
  float hc = !isnan(humsht4x) ? humsht4x : (!isnan(humsht31) ? humsht31 : pmsHum);
  if (!isnan(tc) && !isnan(hc)) {
    ENS160.setTempAndHum(tc, hc);
  }

  ens160StatusRaw = ENS160.getENS160Status();
  ens160Aqi = ENS160.getAQI();
  ens160Tvoc = ENS160.getTVOC();
  ens160Eco2 = ENS160.getECO2();

  bool statusOk = (ens160StatusRaw <= 0x02);
  bool valuesOk = (ens160Aqi >= 1U && ens160Aqi <= 5U &&
                   ens160Tvoc <= 65000U &&
                   ens160Eco2 >= 400U && ens160Eco2 <= 65000U);
  ens160DataValid = statusOk && valuesOk;

  Serial.printf("[ENS160] status=%u aqi=%u tvoc=%u eco2=%u valid=%d\n",
                ens160StatusRaw, ens160Aqi, ens160Tvoc, ens160Eco2,
                ens160DataValid);

  if (ens160DataValid || ens160StatusRaw == 0x01 || ens160StatusRaw == 0x02) {
    if (ens160DataValid) ens160InvalidCount = 0;
    return;
  }

  if (++ens160InvalidCount >= ENS160_INVALID_REINIT_THRESHOLD) {
    ens160InvalidCount = 0;
    recoverI2CBus("ENS160 invalid threshold");
  }
}

void enforceStationStartupConfig() {
  bool changed = false;

  if (!config.sdAutoMount) { config.sdAutoMount = true; changed = true; }
  if (config.sdSavePeriod != 180000UL) { config.sdSavePeriod = 180000UL; changed = true; }
  if (config.httpSendPeriod != 300000UL) { config.httpSendPeriod = 300000UL; changed = true; }
  if (!config.autostart) { config.autostart = true; changed = true; }
  if (config.autostartWaitGps) { config.autostartWaitGps = false; changed = true; }
  if (!config.autoDebug) { config.autoDebug = true; changed = true; }

  if (changed) {
    saveConfig();
    Serial.println("[CONFIG] Station startup enforced: autostart/debug ON, HTTP 5min, SD 3min");
  }
}

// -------------------- OLED Helper --------------------
void oledStatus(const String &l1, const String &l2 = "", const String &l3 = "", const String &l4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 12); u8g2.print(l1);
  u8g2.setCursor(0, 26); u8g2.print(l2);
  u8g2.setCursor(0, 40); u8g2.print(l3);
  u8g2.setCursor(0, 54); u8g2.print(l4);
  u8g2.sendBuffer();
}

// -------------------- AT Helpers --------------------
void atBegin(const String &cmd, const String &expect1, const String &expect2, uint32_t timeout_ms) {
  modem.stream.print("AT");
  modem.stream.println(cmd);
  at.active = true;
  at.expect1 = expect1;
  at.expect2 = expect2;
  at.resp = "";
  at.deadline = millis() + timeout_ms;
}

bool atTick(bool &done, bool &ok) {
  uint32_t t0 = millis();
  while (SerialAT.available()) {
    if ((millis() - t0) >= 20) { done = false; ok = false; return false; }
    String line = SerialAT.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;
    if (line.charAt(0) == '$') { parseNMEA(line); continue; }
    if (!at.active) continue;
    if (at.resp.length() < 2048) { at.resp += line; at.resp += "\n"; }
    if (at.expect1.length() && line.indexOf(at.expect1) >= 0) { done = true; ok = true; at.active = false; return true; }
    if (at.expect2.length() && line.indexOf(at.expect2) >= 0) { done = true; ok = (at.expect2 == "OK"); at.active = false; return true; }
  }
  if (at.active && millis() > at.deadline) { done = true; ok = false; at.active = false; return true; }
  done = false; ok = false; return false;
}

bool atRun(const String &cmd, const String &expect1, const String &expect2, uint32_t timeout_ms) {
  atBegin(cmd, expect1, expect2, timeout_ms);
  bool done = false, ok = false;
  uint32_t hardDeadline = millis() + timeout_ms + 1000;
  while (!done && millis() < hardDeadline) { feedWdt(); if (atTick(done, ok)) break; delay(1); }
  at.active = false;
  return ok;
}

bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms) {
  atBegin(cmd, "OK", "ERROR", timeout_ms);
  bool done = false, ok = false;
  uint32_t hardDeadline = millis() + timeout_ms + 1000;
  while (!done && millis() < hardDeadline) { feedWdt(); if (atTick(done, ok)) break; delay(1); }
  resp = at.resp;
  at.active = false;
  return ok;
}

void updateNetworkInfo() {
  String resp;
  if (sendAtSync("+COPS?", resp, 3000)) {
    int idx = resp.indexOf("+COPS:");
    if (idx >= 0) {
      int start = resp.indexOf('"', idx);
      int end = resp.indexOf('"', start + 1);
      if (start >= 0 && end > start) networkOperator = resp.substring(start + 1, end);
    }
  }
  if (sendAtSync("+CREG?", resp, 2000)) {
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) registrationStatus = "Registered";
    else registrationStatus = "NotRegistered";
  }
}

// -------------------- Telemetry Tx --------------------
String getIdsSensores(const String& deviceId) {
  if (deviceId == "1") return "1028,1029,1029,1030,1030,1030,1030,1030,1031,1032,1032,1032,1032,1032,1033,1042,1042";
  if (deviceId == "2") return "1035,1036,1036,1037,1037,1037,1037,1037,1038,1039,1039,1039,1039,1039,1040,1041,1041";
  if (deviceId == "3") return "1048,1049,1049,1050,1050,1050,1050,1050,1051,1052,1052,1052,1052,1052,1053,1054,1054";
  if (deviceId == "4") return "1055,1056,1056,1057,1057,1057,1057,1057,1058,1059,1059,1059,1059,1059,1060,1061,1061";
  if (deviceId == "5") return "1079,1080,1080,1081,1081,1081,1081,1081,1082,1083,1083,1083,1083,1083,1084,1085,1085";
  if (deviceId == "6") return "1086,1087,1087,1088,1088,1088,1088,1088,1089,1090,1090,1090,1090,1090,1091,1092,1092";
  if (deviceId == "7") return "1093,1094,1094,1095,1095,1095,1095,1095,1096,1097,1097,1097,1097,1097,1098,1099,1099";
  if (deviceId == "8") return "1100,1101,1101,1102,1102,1102,1102,1102,1103,1104,1104,1104,1104,1104,1105,1106,1106";
  if (deviceId == "9") return "1107,1108,1108,1109,1109,1109,1109,1109,1110,1111,1111,1111,1111,1111,1112,1113,1113";
  if (deviceId == "10") return "1114,1115,1115,1116,1116,1116,1116,1116,1117,1118,1118,1118,1118,1118,1119,1120,1120";
  return ""; 
}
bool sendCurrentMeasurement() {
  setStage("sendCurrentMeasurement.build");
  String idsSensores = getIdsSensores(String(DEVICE_ID_STR));
  if (idsSensores == "") return false;

  String v1 = GasOK ? safeFloatStr(gas.readGasConcentrationPPM()) : missingUrlValue();
  String v2 = (ENS160OK && ens160DataValid) ? safeIntStr(ens160Tvoc) : missingUrlValue();
  String v3 = (ENS160OK && ens160DataValid) ? safeIntStr(ens160Eco2) : missingUrlValue();
  String v4 = safeGpsStr(gpsLat), v5 = safeGpsStr(gpsLon), v6 = safeIntStr(csq);
  String v7 = safeNumberStr(gpsSpeedKmh), v8 = safeSatsStr(satellitesStr), v9 = safeFloatStr(batV);
  String v10 = safeFloatStr(pmsTempC), v11 = safeFloatStr(pmsHum);
  String v12 = safeUIntStr(PM1), v13 = safeUIntStr(PM25), v14 = safeUIntStr(PM10);
  String v15 = SDS198OK ? safeUIntStr(SDS198PM100) : missingUrlValue();
  float tSht = SHT4xOK ? tempsht4x : (SHT31OK ? tempsht31 : NAN);
  float hSht = SHT4xOK ? humsht4x : (SHT31OK ? humsht31 : NAN);
  String v16 = safeFloatStr(tSht), v17 = safeFloatStr(hSht);

  String valores = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 + "," + v8 + "," + v9 + "," +
                   v10 + "," + v11 + "," + v12 + "," + v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17;

  String fullUrl = String(API_BASE) + "?idsSensores=" + idsSensores + "&idsVariables=" + GLOBAL_IDS_VARIABLES + "&valores=" + valores;
  Serial.println("[HTTP] GET " + fullUrl);

  setStage("sendCurrentMeasurement.httpGet");
  if (httpGet_webhook(fullUrl)) {
    sendCounter++;
    prefs.begin("system", false); prefs.putUInt("sendCnt", sendCounter); prefs.end();
    return true;
  }
  saveFailedTransmission(fullUrl, "HTTP_FAIL");
  return false;
}

// -------------------- Sensor Refresh --------------------
void refreshSensors2s() {
  if (rtcOK) rtcTempC = rtc.getTemperature();
  if (GasOK) Serial.printf("Gas PPM: %.2f\n", gas.readGasConcentrationPPM());
  if (SHT4xOK) {
    sensors_event_t h, t;
    if (sht4.getEvent(&h, &t)) {
      tempsht4x = t.temperature;
      humsht4x = h.relative_humidity;
      sht4xFailCount = 0;
      Serial.printf("[SHT4X] temp=%.2f hum=%.2f\n", tempsht4x, humsht4x);
    } else {
      logError("I2C_READ_FAIL", "SHT4X.getEvent", "SHT4x read failed");
      if (++sht4xFailCount >= SHT4X_FAIL_REINIT_THRESHOLD) {
        sht4xFailCount = 0;
        recoverI2CBus("SHT4x repeated read failures");
      }
    }
  }
  if (SHT31OK) {
    float t31 = sht31.readTemperature();
    float h31 = sht31.readHumidity();
    if (!isnan(t31)) tempsht31 = t31;
    if (!isnan(h31)) humsht31 = h31;
    Serial.printf("[SHT31] temp=%.2f hum=%.2f\n", tempsht31, humsht31);
  }

  updateEns160State();
}

bool waitForModemAT() {
  uint32_t t0 = millis();
  uint32_t attempt = 0;
  while (millis() - t0 < MODEM_TESTAT_TOTAL_MS) {
    feedWdt(); attempt++;
    if (modem.testAT(MODEM_TESTAT_RETRY_MS)) return true;
    oledStatus("MODEM", "Retry", String(attempt));
    digitalWrite(MODEM_PWRKEY, HIGH); delay(300); digitalWrite(MODEM_PWRKEY, LOW); delay(700);
  }
  return false;
}

void IRAM_ATTR isr_btn2() {
  uint32_t now = millis();
  if (now - lastDebounceTime2 > BTN2_DEBOUNCE_MS) {
    lastDebounceTime2 = now;
    btn2ClickFlag = true;
  }
}

void handleButtonLogic() {
  static bool lastRawBtn1State = HIGH;
  static bool stableBtn1State = HIGH;
  bool currentBtn1State = digitalRead(BUTTON_PIN_1);

  if (currentBtn1State != lastRawBtn1State) {
    lastDebounceTime1 = millis();
  }
  lastRawBtn1State = currentBtn1State;

  if ((millis() - lastDebounceTime1) > BTN1_DEBOUNCE_MS) {
    if (currentBtn1State != stableBtn1State) {
      stableBtn1State = currentBtn1State;
      if (stableBtn1State == LOW) {
        ui_btn1_click();
      }
    }
  }

  if (btn2ClickFlag) {
    btn2ClickFlag = false;
    ui_btn2_click();
  }
}

// -------------------- SETUP --------------------
void setup() {
  delay(300); Serial.begin(115200);
  startWdtOnce(); setStage("setup.start");
  configureI2CBus();
  pixels.begin(); pixels.setPixelColor(0, pixels.Color(0, 50, 100)); pixels.show();

  Serial.println("\n[BOOT] FirmwarePro " + VERSION);
  checkRebootReason();

  prefs.begin("system", false);
  sendCounter = prefs.getUInt("sendCnt", 0); csvFileName = prefs.getString("csvFile", "");
  wasStreamingBeforeBoot = prefs.getBool("streaming", false);
  prefs.end();

  loadConfig();
  enforceStationStartupConfig();
  applyLEDConfig();

  logFilePath = String("/errors_h") + String(DEVICE_ID_STR) + String(".csv");
  failedTxPath = String("/failed_h") + String(DEVICE_ID_STR) + String(".csv");
  AP_SSID_STR = "HIRIPRO_" + String(DEVICE_ID_STR);

  u8g2.begin(); u8g2.setDisplayRotation(config.rotateDisplay ? U8G2_R0 : U8G2_R2);
  lastOledActivity = millis();

  // Animation
  while (logoXOffset < LOGO_FINAL_X || hiriXOffset > HIRI_FINAL_X || proYOffset > PRO_FINAL_Y) {
    feedWdt();
    if (logoXOffset < LOGO_FINAL_X) logoXOffset += 4;
    if (hiriXOffset > HIRI_FINAL_X) hiriXOffset -= 4;
    if (proYOffset > PRO_FINAL_Y) proYOffset -= 1;
    drawAnimation(); delay(20);
  }

  pms.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, Serial2RX_PIN, Serial2TX_PIN);

  if (rtc.begin()) { rtcOK = true; }
  
  spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  SDOK = SD.begin(SD_CS, spiSD);
  if (SDOK) {
    if (csvFileName.length() == 0) csvFileName = generateCSVFileName();
    writeCSVHeader();
    prefs.begin("system", false); prefs.putString("csvFile", csvFileName); prefs.end();
  }

  setStage("sensors.init");
  initI2CSensors();
  if (!gas.begin()) {
    GasOK = false;
    logError("I2C_SENSOR_FAIL", "gas.begin", "Gas sensor not found");
  } else {
    gas.changeAcquireMode(gas.PASSIVITY);
    gas.setTempCompensation(gas.ON);
    GasOK = true;
    Serial.println("[GAS] OK");
  }

  pinMode(BUTTON_PIN_1, INPUT_PULLUP);
  if (BUTTON_PIN_2 >= 0) {
    pinMode(BUTTON_PIN_2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_2), isr_btn2, RISING);
  }

  setStage("modem.boot");
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  pinMode(MODEM_PWRKEY, OUTPUT); pinMode(MODEM_FLIGHT, OUTPUT); digitalWrite(MODEM_FLIGHT, HIGH);
  
  oledStatus("MODEM", "Starting...");
  if (waitForModemAT()) {
    atRun("+CEDRXS=0", "OK", "ERROR", 1500);
    atRun("+CPSMS=0", "OK", "ERROR", 1500);
    oledStatus("NET", "Attach...");
    if (modem.waitForNetwork(60000)) {
      if (modem.gprsConnect(apn, gprsUser, gprsPass)) oledStatus("NET", "OK");
    }
    detectAndEnableXtra();
  }
  
  gnssBringUp();
  if (config.autostart) {
    streaming = true;
    loggingEnabled = SDOK;
    lastHttpSend = millis() - config.httpSendPeriod;
    lastSdSave = millis();
    Serial.println("[BOOT] Autostart enabled: immediate HTTP, SD every 3 min");
  }
  if (config.autoDebug) {
    uiFullMode = true;
    debugScreenIndex = 0;
    lastDebugRotationMs = millis();
    showMessage("MODO DEBUG");
    Serial.println("[BOOT] Auto Debug enabled");
  }
  setStage("setup.done");
}

// -------------------- LOOP --------------------
bool FirstLoop = true;

void loop() {
  feedWdt(); setStage("loop.start"); printHeartbeat();
  handleButtonLogic();

  if (wifiModeActive) {
    dnsServer.processNextRequest(); server.handleClient();
    if (haveFix) gnssWatchdog();
    return;
  }

  gnssWatchdog(); gnssDiagTick(); gnssDebugPollAsync();

  static uint32_t lastSensorUpdateMs = 0;
  if (millis() - lastSensorUpdateMs >= 2000) {
    lastSensorUpdateMs = millis();
    refreshSensors2s();
  }

  if (FirstLoop) {
    csq = modem.getSignalQuality(); updateNetworkInfo();
    FirstLoop = false;
  }

  readPMS();
  static uint32_t lastLedUpdateMs = 0;
  if (millis() - lastLedUpdateMs >= 300) { lastLedUpdateMs = millis(); updatePmLed((float)PM25); }

  byte frame[10];
  if (readFrameSDS198(frame)) {
    SDS198PM100 = (uint16_t)((frame[5] << 8) | frame[4]);
    SDS198OK = true;
  }

  static uint32_t lastAtTick = 0;
  if (millis() - lastAtTick >= 50) {
    lastAtTick = millis(); bool d, o; (void)atTick(d, o);
  }

  if (millis() - lastBatSample >= BAT_SAMPLE_INTERVAL_MS) {
    lastBatSample = millis(); batSampleSum += analogRead(BAT_PIN); batSampleCount++;
    if (batSampleCount >= NUM_SAMPLES) {
      batV = (batSampleSum / NUM_SAMPLES / 4095.0f) * 3.3f * 2.0f * 1.15f;
      batSampleSum = 0; batSampleCount = 0;
    }
  }

  static uint32_t lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 60) { lastDisplayUpdate = millis(); renderDisplay(); }

  if (loggingEnabled && (millis() - lastSdSave >= config.sdSavePeriod)) {
    lastSdSave = millis(); Serial.println("[SD] Save");
    lastSdOk = saveCSVData(); lastSdActivityMs = millis();
  }

  if (streaming && (millis() - lastHttpSend >= config.httpSendPeriod)) {
    lastHttpSend = millis(); Serial.println("[HTTP] Send");
    lastHttpOk = sendCurrentMeasurement();
    hasHttpAttempted = true;
    lastHttpActivityMs = millis();
  }

  processSerialCommand();
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\PMSandSensorEXTRA.ino"
// -------------------- PMS non-blocking parser (now with T/RH)
// --------------------

#include "config.h"
extern Adafruit_NeoPixel pixels;
extern SoftwareSerial pms;

void readPMS() {
  // Ingest bytes
  while (pms.available() > 0) {
    int c = pms.read();
    if (c < 0)
      break;
    lastPmsSeen = millis();
    if (pmsHead < sizeof(pmsBuf))
      pmsBuf[pmsHead++] = (uint8_t)c;
    else {
      memmove(pmsBuf, pmsBuf + 1, sizeof(pmsBuf) - 1);
      pmsBuf[sizeof(pmsBuf) - 1] = (uint8_t)c;
    }
  }
  // Scan frames (32 bytes) with header 0x42 0x4D
  size_t i = 0;
  while (pmsHead >= 32 && i + 32 <= pmsHead) {
    uint8_t *frm = &pmsBuf[i];

    // Require header alignment to avoid false positives
    if (!(frm[0] == 0x42 && frm[1] == 0x4D)) {
      ++i;
      continue;
    }

    uint16_t cr1 = ((uint16_t)frm[30] << 8) | frm[31];
    uint16_t cr2 = 0;
    for (int k = 0; k < 30; ++k)
      cr2 += frm[k];
    if (cr1 == cr2) {
      // Standard concentration fields
      PM1 = ((uint16_t)frm[10] << 8) | frm[11];
      PM25 = ((uint16_t)frm[12] << 8) | frm[13];
      PM10 = ((uint16_t)frm[14] << 8) | frm[15];

      // Optional ST fields (0.1 units): bytes 24..27 (T,RH)
      uint16_t t10 = ((uint16_t)frm[24] << 8) | frm[25];
      uint16_t h10 = ((uint16_t)frm[26] << 8) | frm[27];
      float tC = t10 / 10.0f;
      float rH = h10 / 10.0f;
      if ((t10 != 0 || h10 != 0) && tC > -40 && tC < 85 && rH >= 0 &&
          rH <= 100) {
        pmsTempC = tC;
        pmsHum = rH;
      } else {
        pmsTempC = NAN;
        pmsHum = NAN;
      }

      size_t remain = pmsHead - (i + 32);
      memmove(pmsBuf, &frm[32], remain);
      pmsHead = remain;
      i = 0;
    } else {
      ++i;
    }
  }
  if (i > 0 && i < pmsHead) {
    memmove(pmsBuf, pmsBuf + i, pmsHead - i);
    pmsHead -= i;
  } else if (i >= pmsHead) {
    pmsHead = 0;
  }
}

// -------------------- PM2.5 smooth gradient (15/25/50) --------------------
static inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  float v = a + (b - a) * t;
  if (v < 0)
    v = 0;
  if (v > 255)
    v = 255;
  return (uint8_t)v;
}
void updatePmLed(float pm25) {
  uint8_t r = 0, g = 0, b = 0;
  if (pm25 <= 15.0f) {
    float t = pm25 / 15.0f;
    r = 0;
    g = lerp8(60, 127, t); // Mitad del brillo (120->60, 255->127)
    b = 0;
  } else if (pm25 <= 25.0f) {
    float t = (pm25 - 15.0f) / 10.0f;
    r = lerp8(0, 127, t); // Mitad del brillo (255->127)
    g = 127;              // Mitad del brillo (255->127)
    b = 0;
  } else if (pm25 <= 50.0f) {
    float t = (pm25 - 25.0f) / 25.0f;
    r = 127;              // Mitad del brillo (255->127)
    g = lerp8(127, 0, t); // Mitad del brillo (255->127)
    b = 0;
  } else {
    r = 127; // Mitad del brillo (255->127)
    g = 0;
    b = 0;
  }
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

//-------------------------SDS198 non-blocking parser (usando Serial2 UART2)-------------------------
uint8_t sdsBuf[16];
uint8_t sdsHead = 0;

// Función para leer una trama de datos del sensor de forma asíncrona.
bool readFrameSDS198(byte* outFrame) {
  while (Serial2.available() > 0) {
    int c = Serial2.read();
    if (c < 0) break;
    
    // Si la cabecera no se ha encontrado y recibimos algo distinto, lo ignoramos
    if (sdsHead == 0 && (byte)c != HEADER) continue;
    
    if (sdsHead < sizeof(sdsBuf)) {
      sdsBuf[sdsHead++] = (uint8_t)c;
    } else {
      memmove(sdsBuf, sdsBuf + 1, sizeof(sdsBuf) - 1);
      sdsBuf[sizeof(sdsBuf) - 1] = (uint8_t)c;
    }
  }
  
  // Buscar trama completa de 10 bytes
  size_t i = 0;
  while (sdsHead >= 10 && i + 10 <= sdsHead) {
    uint8_t *frm = &sdsBuf[i];
    if (frm[0] == HEADER && frm[1] == CMD && frm[9] == TAIL) {
      byte sum = 0;
      for (int k = 2; k <= 7; k++) {
        sum += frm[k];
      }
      if (sum == frm[8]) {
        // Trama válida encontrada
        memcpy(outFrame, frm, 10);
        size_t remain = sdsHead - (i + 10);
        memmove(sdsBuf, &sdsBuf[i + 10], remain);
        sdsHead = remain;
        return true;
      } else {
        ++i;
      }
    } else {
      ++i;
    }
  }
  
  if (i > 0 && i < sdsHead) {
    memmove(sdsBuf, sdsBuf + i, sdsHead - i);
    sdsHead -= i;
  } else if (i >= sdsHead) {
    sdsHead = 0;
  }
  
  return false;
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\animacion.ino"
#define LOGO_WIDTH 40   // 1/3 del tamaño original
#define LOGO_HEIGHT 34  // 1/3 del tamaño original

const unsigned char logo_bits[] PROGMEM = {
	0x00, 0x0f, 0x00, 0x3c, 0x00, 0x80, 0x1f, 0x00, 0x7e, 0x00, 0xc0, 0x3f, 0x00, 0xff, 0x00, 0xe0, 
	0x7f, 0x80, 0xff, 0x01, 0xe0, 0xff, 0xff, 0xff, 0x01, 0xe0, 0xff, 0xff, 0xff, 0x01, 0xe0, 0x7f, 
	0x80, 0xff, 0x01, 0xc0, 0x3f, 0x00, 0xff, 0x00, 0x80, 0x1f, 0x00, 0x7e, 0x00, 0x00, 0x0f, 0x00, 
	0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x0f, 0x00, 
	0x3f, 0x7e, 0x80, 0x1f, 0x80, 0x7f, 0xff, 0xc0, 0x3f, 0xc0, 0xf3, 0xff, 0xe1, 0x7f, 0xc0, 0xf3, 
	0xff, 0xe1, 0xff, 0xff, 0xc0, 0xff, 0xe1, 0xff, 0xff, 0xc0, 0xff, 0xe1, 0x7f, 0xc0, 0xf3, 0xff, 
	0xc0, 0x3f, 0xc0, 0xf3, 0x7e, 0x80, 0x1f, 0x80, 0x7f, 0x3c, 0x00, 0x0f, 0x00, 0x3f, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x3c, 0x00, 0x80, 0x1f, 0x00, 
	0x7e, 0x00, 0xc0, 0x3f, 0x00, 0xff, 0x00, 0xe0, 0x7f, 0x80, 0xff, 0x01, 0xe0, 0xff, 0xff, 0xff, 
	0x01, 0xe0, 0xff, 0xff, 0xff, 0x01, 0xe0, 0x7f, 0x80, 0xff, 0x01, 0xc0, 0x3f, 0x00, 0xff, 0x00, 
	0x80, 0x1f, 0x00, 0x7e, 0x00, 0x00, 0x0f, 0x00, 0x3c, 0x00
};



void drawAnimation() {
  if (!oledOK) return;
  u8g2.clearBuffer();
  
  // Dibujar logo escalado
  //u8g2.setDrawColor(0);  // Establece el color de dibujo a negro
  u8g2.drawXBMP(logoXOffset, 10, LOGO_WIDTH, LOGO_HEIGHT, logo_bits);
  //u8g2.setDrawColor(1);  // Establece el color de dibujo a blanco
  
  // Dibujar "HIRI" más grande
  u8g2.setFont(u8g2_font_fub30_tr);  // Fuente aún más grande para HIRI
  u8g2.drawStr(hiriXOffset, HIRI_FINAL_Y, "HIRI");
  
  // Dibujar "PRO" más pequeño, usando las variables de posición final
  u8g2.setFont(u8g2_font_6x10_tr);  // Fuente muy pequeña para PRO
  u8g2.drawStr(PRO_FINAL_X, proYOffset, "PRO");  // Usa PRO_FINAL_X para la posición horizontal
  
  u8g2.sendBuffer();
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\config.ino"
// -------------------- Configuration System --------------------
// Sistema de configuración persistente en flash (Preferences)
// Configuración accesible por comandos seriales
// NOTA: struct SystemConfig y variable config están declaradas en el archivo
// ESTA confirguracion es la final para HIRIVALPO
// principal

// -------------------- Load Configuration --------------------
void loadConfig() {
  prefs.begin("config", false);

  // SD
  config.sdAutoMount = prefs.getBool("sdAuto", true);//vamos a cargar la sd automaticamente true
  config.sdSavePeriod = prefs.getUInt("sdSavePer", 180000); // 3 minutos

  // HTTP
  config.httpSendPeriod = prefs.getUInt("httpPer", 300000); // 5 minutos
  config.httpTimeout = prefs.getUShort("httpTO", 15);

  // Display
  config.oledAutoOff = prefs.getBool("oledOff", false);
  config.oledTimeout = prefs.getUInt("oledTO", 120000);

  // Power
  config.ledEnabled = prefs.getBool("ledEn", true);
  config.ledBrightness = prefs.getUChar("ledBr", 50);

  // Autostart
  config.autostart = prefs.getBool("autoStart", true);
  config.autostartWaitGps = prefs.getBool("autoGPS", false);
  config.autostartGpsTimeout = prefs.getUShort("autoGPSTO", 600);
  config.autoDebug = prefs.getBool("autoDbg", true);

  // System
  config.rotateDisplay = prefs.getBool("rotDisp", true); //false originalmente rotado para estas estaciones
  config.scheduledRebootHours = prefs.getUShort("rebootH", 3);
  config.gnssEnabled = prefs.getBool("gnssEn", false);

  // GNSS Mode
  config.gnssMode = prefs.getUChar("gnssMode", 15);

  prefs.end();

  Serial.println("[CONFIG] Loaded from flash");
  Serial.printf("[CONFIG] SD auto-mount: %s, save period: %lums\n",
                config.sdAutoMount ? "ON" : "OFF", config.sdSavePeriod);
  Serial.printf("[CONFIG] HTTP send period: %lums, timeout: %us\n",
                config.httpSendPeriod, config.httpTimeout);
  Serial.printf("[CONFIG] OLED auto-off: %s, timeout: %lums\n",
                config.oledAutoOff ? "ON" : "OFF", config.oledTimeout);
  Serial.printf("[CONFIG] LED enabled: %s, brightness: %u%%\n",
                config.ledEnabled ? "YES" : "NO", config.ledBrightness);
  Serial.printf("[CONFIG] Autostart: %s, wait GPS: %s, GPS timeout: %us\n",
                config.autostart ? "YES" : "NO",
                config.autostartWaitGps ? "YES" : "NO",
                config.autostartGpsTimeout);
  Serial.printf("[CONFIG] Auto Debug: %s\n", config.autoDebug ? "YES" : "NO");
  Serial.printf("[CONFIG] Rotate Display: %s\n", config.rotateDisplay ? "YES" : "NO");
  if (config.scheduledRebootHours == 0) {
    Serial.println("[CONFIG] Scheduled reboot: disabled");
  } else {
    Serial.printf("[CONFIG] Scheduled reboot: every %u hours\n",
                  config.scheduledRebootHours);
  }
  Serial.printf("[CONFIG] GNSS enabled: %s\n", config.gnssEnabled ? "YES" : "NO");
  Serial.printf("[CONFIG] GNSS mode: %u\n", config.gnssMode);

}

// -------------------- Save Configuration --------------------
void saveConfig() {
  prefs.begin("config", false);

  // SD
  prefs.putBool("sdAuto", config.sdAutoMount);
  prefs.putUInt("sdSavePer", config.sdSavePeriod);

  // HTTP
  prefs.putUInt("httpPer", config.httpSendPeriod);
  prefs.putUShort("httpTO", config.httpTimeout);

  // Display
  prefs.putBool("oledOff", config.oledAutoOff);
  prefs.putUInt("oledTO", config.oledTimeout);

  // Power
  prefs.putBool("ledEn", config.ledEnabled);
  prefs.putUChar("ledBr", config.ledBrightness);

  // Autostart
  prefs.putBool("autoStart", config.autostart);
  prefs.putBool("autoGPS", config.autostartWaitGps);
  prefs.putUShort("autoGPSTO", config.autostartGpsTimeout);
  prefs.putBool("autoDbg", config.autoDebug);

  // System
  prefs.putBool("rotDisp", config.rotateDisplay);
  prefs.putUShort("rebootH", config.scheduledRebootHours);
  prefs.putBool("gnssEn", config.gnssEnabled);

  // GNSS Mode
  prefs.putUChar("gnssMode", config.gnssMode);

  prefs.end();

  Serial.println("[CONFIG] Saved to flash");
}

// -------------------- Reset to Defaults -------------------- 
//cambiado a modo estacion
void configSetDefaults() {
  config.sdAutoMount = true; // NO montar en boot por defecto false
  config.sdSavePeriod = 180000; // 3 minutos

  config.httpSendPeriod = 300000; // 5 minutos 
  config.httpTimeout = 15;      // 15 segundos

  config.oledAutoOff = false;  // Siempre encendida
  config.oledTimeout = 120000; // 2 minutos

  config.ledEnabled = true;  // LED habilitado
  config.ledBrightness = 50; // 50% brillo

  config.autostart = true;         // SI autostart
  config.autostartWaitGps = false;  // NO esperar GPS
  config.autostartGpsTimeout = 600; // 10 minutos (default)
  config.autoDebug = true;         // SI autodebug

  config.rotateDisplay = false;     // Display NO rotado
  config.scheduledRebootHours = 3;   // Reinicio programado cada 3 horas; 0 deshabilita
  config.gnssEnabled = false;       // GNSS apagado por defecto

  config.gnssMode = 15; // Todas las constelaciones (default)

  Serial.println("[CONFIG] Reset to defaults");
}

// -------------------- Print Configuration --------------------
void printConfig() {
  Serial.println("=== HIRI PRO Configuration ===");

  Serial.println("\n[SD Card]");
  Serial.printf("  Auto-mount on boot: %s\n",
                config.sdAutoMount ? "ON" : "OFF");
  Serial.printf("  Save period:        %lu ms (%lu s)\n", config.sdSavePeriod,
                config.sdSavePeriod / 1000);

  Serial.println("\n[HTTP Transmission]");
  Serial.printf("  Send period:        %lu ms (%lu s)\n", config.httpSendPeriod,
                config.httpSendPeriod / 1000);
  Serial.printf("  Timeout:            %u seconds\n", config.httpTimeout);

  Serial.println("\n[Display OLED]");
  Serial.printf("  Auto-off:           %s\n",
                config.oledAutoOff ? "ON" : "OFF");
  Serial.printf("  Timeout:            %lu ms (%lu s)\n", config.oledTimeout,
                config.oledTimeout / 1000);

  Serial.println("\n[Power/LED]");
  Serial.printf("  NeoPixel enabled:   %s\n", config.ledEnabled ? "YES" : "NO");
  Serial.printf("  Brightness:         %u%%\n", config.ledBrightness);

  Serial.println("\n[Autostart]");
  Serial.printf("  Autostart:          %s\n", config.autostart ? "YES" : "NO");
  Serial.printf("  Wait for GPS fix:   %s\n",
                config.autostartWaitGps ? "YES" : "NO");
  Serial.printf("  GPS timeout:        %u seconds (%u min)\n",
                config.autostartGpsTimeout, config.autostartGpsTimeout / 60);
  Serial.printf("  Auto Debug:         %s\n", config.autoDebug ? "YES" : "NO");

  Serial.println("\n[System]");
  Serial.printf("  Rotate display:     %s\n", config.rotateDisplay ? "YES" : "NO");
  if (config.scheduledRebootHours == 0) {
    Serial.println("  Scheduled reboot:   OFF");
  } else {
    Serial.printf("  Scheduled reboot:   every %u hours\n",
                  config.scheduledRebootHours);
  }

  Serial.println("\n[GNSS Configuration]");
  Serial.printf("  Enabled:            %s\n", config.gnssEnabled ? "YES" : "NO");
  Serial.printf("  Mode:               %u ", config.gnssMode);
  switch (config.gnssMode) {
  case 1:
    Serial.println("(GPS only)");
    break;
  case 3:
    Serial.println("(GPS + GLONASS)");
    break;
  case 5:
    Serial.println("(GPS + BEIDOU)");
    break;
  case 7:
    Serial.println("(GPS + GLONASS + BEIDOU)");
    break;
  case 15:
    Serial.println("(ALL: GPS + GLONASS + GALILEO + BEIDOU)");
    break;
  default:
    Serial.println("(Unknown)");
    break;
  }

  Serial.println();
}

// -------------------- Print SD Info --------------------
void printSDInfo() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted");
    return;
  }

  Serial.println("=== SD Card Info ===");
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
  uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
  Serial.printf("Card size:    %llu MB\n", cardSize);
  Serial.printf("Total:        %llu MB\n", totalBytes);
  Serial.printf("Used:         %llu MB\n", usedBytes);
  Serial.printf("Free:         %llu MB\n", totalBytes - usedBytes);
  Serial.printf("Current file: %s\n", csvFileName.c_str());
}

// -------------------- Print SD File List --------------------
void printSDFileList() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted");
    return;
  }

  Serial.println("=== SD Files ===");
  File root = SD.open("/");
  if (!root) {
    Serial.println("[SD] Cannot open root");
    return;
  }

  File file = root.openNextFile();
  int count = 0;
  while (file) {
    if (!file.isDirectory()) {
      Serial.printf("  %s - %lu bytes\n", file.name(),
                    (unsigned long)file.size());
      count++;
    }
    file = root.openNextFile();
  }
  Serial.printf("Total: %d files\n", count);
}

// -------------------- Apply LED Configuration --------------------
void applyLEDConfig() {
  if (!config.ledEnabled) {
    pixels.clear();
    pixels.show();
    Serial.println("[LED] Disabled by config");
  } else {
    // Convertir porcentaje a 0-255
    uint8_t brightness = map(config.ledBrightness, 0, 100, 0, 255);
    pixels.setBrightness(brightness);
    pixels.setPixelColor(0, pixels.Color(0, 50, 100));
    pixels.show();
    Serial.printf("[LED] Brightness set to %u%% (%u/255)\n",
                  config.ledBrightness, brightness);
  }
}

// -------------------- Clear SD Card --------------------
void clearSDCard() {
  if (!SDOK) {
    Serial.println("[SD] Not mounted - cannot clear");
    return;
  }

  Serial.println("[SD] Starting to delete all files...");
  File root = SD.open("/");
  if (!root) {
    Serial.println("[SD] Cannot open root directory");
    return;
  }

  int deletedCount = 0;
  int failedCount = 0;

  // First pass: collect all filenames
  String fileNames[50]; // Max 50 files
  int fileCount = 0;

  File file = root.openNextFile();
  while (file && fileCount < 50) {
    if (!file.isDirectory()) {
      fileNames[fileCount++] = String("/") + String(file.name());
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  // Second pass: delete all collected files
  for (int i = 0; i < fileCount; i++) {
    Serial.print("[SD] Deleting: ");
    Serial.print(fileNames[i]);

    if (SD.remove(fileNames[i].c_str())) {
      Serial.println(" - OK");
      deletedCount++;
    } else {
      Serial.println(" - FAILED");
      failedCount++;
    }
  }

  Serial.println("[SD] Clear complete:");
  Serial.printf("  Deleted: %d files\n", deletedCount);
  if (failedCount > 0) {
    Serial.printf("  Failed:  %d files\n", failedCount);
  }

  // Reset CSV filename in preferences since all files were deleted
  csvFileName = "";
  prefs.begin("system", false);
  prefs.putString("csvFile", "");
  prefs.end();
  Serial.println("[SD] CSV filename cleared from memory");
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\gps.ino"
// -------------------- External Variables --------------------
#include "config.h"
extern SystemConfig config;
extern struct AtSession at;
extern TinyGsm modem;

extern String gpsLat, gpsLon, gpsDate, gpsTime, gpsStatus, gpsAlt, gpsSpeedKmh;
extern String satellitesStr, hdopStr;
extern bool haveFix, ttffPrinted;
extern uint32_t gnssStartMs, lastFixMs, lastGgaMs, lastNmeaMs, lastNmeaSeenMs;
extern uint16_t nmeaCount1s, nmeaRate;
extern uint32_t nmeaRefMs;
extern uint8_t fixQLast;
extern bool gnssFixReported;

// GSA/GSV
extern uint8_t gsaFixType, gsaSatsUsed;
extern float gsaPdop, gsaHdop2, gsaVdop;
extern uint16_t gsvSatsInView;
extern float gsvSnrAvg, gsvSnrMax, gsvSnrAcc;
extern uint32_t gsvLastMs;
extern int gsvSnrCnt;

extern const char apn[];
extern const char gprsUser[];
extern const char gprsPass[];
extern bool xtraSupported, xtraLastOk;
extern uint32_t lastXtraDownload;

// GnssDbgState local to this module
enum GnssDbgState {
  GNSS_DBG_IDLE,
  GNSS_DBG_ASK_STATUS,
  GNSS_DBG_WAIT_STATUS,
  GNSS_DBG_ASK_INFO,
  GNSS_DBG_WAIT_INFO
};
GnssDbgState gnssDbgState = GNSS_DBG_IDLE;
uint32_t gnssDbgNextAt = 0;

// -------------------- GPS parsing helpers --------------------
void splitSentence(const String &sentence, char delimiter, String fields[],
                   int expectedFields) {
  int fieldIndex = 0, start = 0, end = sentence.indexOf(delimiter);
  while (end != -1 && fieldIndex < expectedFields) {
    fields[fieldIndex++] = sentence.substring(start, end);
    start = end + 1;
    end = sentence.indexOf(delimiter, start);
  }
  if (fieldIndex < expectedFields)
    fields[fieldIndex++] = sentence.substring(start);
}

// Convierte coordenadas NMEA (ddmm.mmmm / dddmm.mmmm) a grados decimales.
// Aplica signo negativo para hemisferios Sur/Oeste.
float convertToDecimal(String coord, String dir) {
  int degDigits = (dir == "N" || dir == "S") ? 2 : 3;
  if ((int)coord.length() < degDigits)
    return NAN;
  int degrees = coord.substring(0, degDigits).toInt();
  float minutes = coord.substring(degDigits).toFloat();
  float dec = degrees + minutes / 60.0f;
  if (dir == "S" || dir == "W")
    dec = -dec;
  return dec;
}

// -------------------- GNSS bring-up + watchdog --------------------
// Reinicia estado GNSS/TTFF al arrancar o reiniciar el subsistema satelital.
// Evita arrastrar flags de fix anteriores.
static inline void resetGnssFlagsAfterStart() {
  gnssStartMs = millis();
  // Reset de estado TTFF
  gnssFixReported = false;
  ttffPrinted = false;
  haveFix = false;
  fixQLast = 0;
  lastFixMs = 0;
}

// Parsea sentencia GSA para extraer tipo de fix, satélites usados y DOPs.
// Alimenta métricas de calidad GNSS para diagnóstico.
void parseGSA(const String &s) {
  // $xxGSA,mode,fixType,sv1,...,sv12,PDOP,HDOP,VDOP*CS
  const int N = 20;
  String f[N];
  splitSentence(s, ',', f, N);

  gsaFixType = f[2].toInt(); // 1/2/3
  uint8_t used = 0;
  for (int i = 3; i <= 14; ++i)
    if (f[i].length() > 0)
      used++;
  gsaSatsUsed = used;

  gsaPdop = f[15].length() ? f[15].toFloat() : NAN;
  gsaHdop2 = f[16].length() ? f[16].toFloat() : NAN;
  gsaVdop = f[17].length() ? f[17].toFloat() : NAN;
}

// Parsea GSV y calcula satélites en vista + estadísticas de SNR.
// Se usa para evaluar calidad de señal satelital en campo.
void parseGSV(const String &s) {
  // $xxGSV,total,msgnum,sats, (prn,elev,az,snr) x up to 4
  const int N = 20;
  String f[N];
  splitSentence(s, ',', f, N);

  int total = f[1].toInt();
  int num = f[2].toInt();
  int sats = f[3].toInt();
  if (num == 1) {
    gsvSnrAcc = 0;
    gsvSnrCnt = 0;
    gsvSnrMax = 0;
  }

  for (int i = 4; i + 3 < N; i += 4) {
    if (f[i + 3].length()) {
      int snr = f[i + 3].toInt();
      if (snr > 0) {
        gsvSnrAcc += snr;
        gsvSnrCnt++;
        if (snr > gsvSnrMax)
          gsvSnrMax = snr;
      }
    }
  }

  if (num == total || total == 0) {
    gsvSatsInView = sats;
    gsvSnrAvg = (gsvSnrCnt > 0) ? (gsvSnrAcc / gsvSnrCnt) : 0;
    gsvLastMs = millis();
  }
}

// Parsea GGA (fix, satélites, HDOP, altitud, lat/lon).
// Actualiza haveFix y timestamps de última solución válida.
void parseGGA(const String &s) {
  const int N = 15;
  String f[N];
  splitSentence(s, ',', f, N);
  String lat = f[2], latD = f[3], lon = f[4], lonD = f[5];
  String fixQ = f[6];
  satellitesStr = f[7];
  hdopStr = f[8];
  String alt = f[9];

  lastGgaMs = millis();
  fixQLast = (uint8_t)fixQ.toInt();
  if (fixQLast >= 1) {
    haveFix = true;
    lastFixMs = lastGgaMs;
  } else {
    haveFix = false;
  }

  float dlat = convertToDecimal(lat, latD);
  float dlon = convertToDecimal(lon, lonD);
  if (!isnan(dlat) && !isnan(dlon)) {
    gpsLat = String(dlat, 6);
    gpsLon = String(dlon, 6);
  }
  gpsAlt = alt;
  gpsStatus = (fixQ.toInt() >= 1) ? "Fix" : "NoFix";
}

// Parsea RMC para estado de navegación, fecha/hora y posición.
// Si no hay validez ('A'), fuerza estado NoFix.
void parseRMC(const String &s) {
  const int N = 12;
  String f[N];
  splitSentence(s, ',', f, N);
  String t = f[1], st = f[2], lat = f[3], latD = f[4], lon = f[5], lonD = f[6],
         date = f[9];
  float dlat = convertToDecimal(lat, latD);
  float dlon = convertToDecimal(lon, lonD);
  if (!isnan(dlat) && !isnan(dlon)) {
    gpsLat = String(dlat, 6);
    gpsLon = String(dlon, 6);
  }
  gpsStatus = (st == "A") ? "Fix" : "NoFix";
  if (st != "A") {
    haveFix = false;
  }
  if (t.length() >= 6)
    gpsTime =
        t.substring(0, 2) + ":" + t.substring(2, 4) + ":" + t.substring(4, 6);
  if (date.length() == 6)
    gpsDate = date.substring(0, 2) + "/" + date.substring(2, 4) + "/20" +
              date.substring(4, 6);
}

// Parsea VTG para extraer velocidad en km/h.
// Mantiene velocidad GNSS desacoplada de GGA/RMC.
void parseVTG(const String &s) {
  const int N = 10;
  String f[N];
  splitSentence(s, ',', f, N);
  String kmh = (f[7].length() > 0) ? f[7] : "0.0";
  gpsSpeedKmh = kmh;
}

// Talker-agnostic (GP/GN/GA/GL/BD/GB/QZ...): detecta tipo por [3..5]
// Router de sentencias NMEA por tipo (GGA/RMC/VTG/GSA/GSV).
// Actualiza heartbeat NMEA y contadores de tasa.
void parseNMEA(const String &line) {
  lastNmeaMs = millis();
  nmeaCount1s++;
  lastNmeaSeenMs = lastNmeaMs; // heartbeat NMEA

  if (line.length() < 7 || line.charAt(0) != '$')
    return;
  String typ = line.substring(3, 6);

  if (typ == "GGA")
    parseGGA(line);
  else if (typ == "RMC")
    parseRMC(line);
  else if (typ == "VTG")
    parseVTG(line);
  else if (typ == "GSA")
    parseGSA(line);
  else if (typ == "GSV")
    parseGSV(line);
}

// -------------------- GNSS bring-up functions --------------------
// Intentar configurar el modo GNSS según config, con fallback automático
// Configura constelación GNSS según preferencia y aplica fallback automático.
// Reduce fallos de compatibilidad entre firmwares de módem.
bool setGnssAllWithFallback() {
  // Intentar primero el modo configurado por el usuario
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "+CGNSSMODE=%u,1", config.gnssMode);

  if (atRun(cmd, "OK", "ERROR", 2000)) {
    Serial.print("[GNSS] Mode set: ");
    Serial.print(config.gnssMode);
    switch (config.gnssMode) {
    case 1:
      Serial.println(" (GPS only)");
      break;
    case 3:
      Serial.println(" (GPS + GLONASS)");
      break;
    case 5:
      Serial.println(" (GPS + BEIDOU)");
      break;
    case 7:
      Serial.println(" (GPS + GLONASS + BEIDOU)");
      break;
    case 15:
      Serial.println(" (ALL: GPS + GLONASS + GALILEO + BEIDOU)");
      break;
    default:
      Serial.println(" (Unknown)");
      break;
    }
    return true;
  }

  // Si falla el modo configurado, intentar fallback con modos más simples
  Serial.print("[GNSS] Configured mode ");
  Serial.print(config.gnssMode);
  Serial.println(" failed, trying fallback...");

  const char *fallbackTries[] = {
      "+CGNSSMODE=7,1", // GPS+GLONASS+BEIDOU
      "+CGNSSMODE=3,1", // GPS+GLONASS
      "+CGNSSMODE=1,1"  // GPS only
  };

  for (size_t i = 0; i < sizeof(fallbackTries) / sizeof(fallbackTries[0]);
       ++i) {
    if (atRun(fallbackTries[i], "OK", "ERROR", 2000)) {
      Serial.print("[GNSS] Fallback mode set: ");
      Serial.println(fallbackTries[i]);
      return true;
    }
  }

  Serial.println("[GNSS] All attempts to set constellation FAILED");
  return false;
}

// Secuencia de encendido GNSS/NMEA y configuración base del receptor.
// Deja el módulo listo para parseo continuo a 1 Hz.
void gnssBringUp() {
  if (!config.gnssEnabled) {
    Serial.println("[GNSS] Disabled by config");
    gpsStatus = "Disabled";
    gpsLat = "N/A";
    gpsLon = "N/A";
    gpsDate = "N/A";
    gpsTime = "N/A";
    gpsAlt = "N/A";
    gpsSpeedKmh = "N/A";
    satellitesStr = "N";
    hdopStr = "N/A";
    haveFix = false;
    return;
  }
  Serial.println("[GNSS] Bring-up...");
  atRun("+CGPS=0", "OK", "ERROR", 5000);
  atRun("+CGNSSPWR=1", "OK", "ERROR", 2000);

  (void)setGnssAllWithFallback(); // multi-constelación con fallback

  atRun("+CGPSNMEA=200191", "OK", "ERROR", 2000);
  atRun("+CGPSNMEARATE=1", "OK", "ERROR", 2000);
  atRun("+CGPS=1", "OK", "ERROR", 8000);
  // NMEA Output to AT port (RMC,GGA,VTG on AT port)
  atRun("+CGPSINFOCFG=1,31", "OK", "ERROR", 2000);
  Serial.println("[GNSS] ON (NMEA 1 Hz)");
  resetGnssFlagsAfterStart();
}

// Reinicio HOT del motor GNSS conservando más contexto satelital.
// Se usa como recuperación rápida ante pérdida parcial de fix.
void gnssHotRestart() {
  if (!config.gnssEnabled)
    return;
  Serial.println("[GNSS] HOT restart");
  atRun("+CGPSRST=0", "OK", "ERROR", 3000);
  resetGnssFlagsAfterStart();
}

// Reinicio WARM del GNSS para recuperación más profunda.
// Se activa cuando no se recupera fix tras período extendido.
void gnssWarmRestart() {
  if (!config.gnssEnabled)
    return;
  Serial.println("[GNSS] WARM restart");
  atRun("+CGPSRST=1", "OK", "ERROR", 3000);
  resetGnssFlagsAfterStart();
}

// Watchdog de GNSS: decide HOT/WARM restart según tiempo sin fix.
// Mantiene resiliencia sin bloquear el loop principal.
void gnssWatchdog() {
  if (!config.gnssEnabled)
    return;
  if (haveFix)
    return;
  uint32_t alive = millis() - gnssStartMs;
  if (alive > 300000UL) {
    gnssWarmRestart();
  } else if (alive > 120000UL) {
    gnssHotRestart();
  }
}

// -------------------- Async GNSS diag & debug (non-blocking)
// --------------------
// Calcula e imprime diagnósticos periódicos de salud GNSS/NMEA.
// Incluye tasa de tramas, edad de fix y métricas GSA/GSV.
void gnssDiagTick() {
  if (!config.gnssEnabled)
    return;
  const uint32_t now = millis();

  // Calcular tasa NMEA cada 1s
  if (now - nmeaRefMs >= 1000) {
    nmeaRefMs = now;
    nmeaRate = nmeaCount1s; // aprox Hz
    nmeaCount1s = 0;
  }

  // Edades (en s)
  uint32_t ageNmea = (lastNmeaMs ? (now - lastNmeaMs) / 1000UL : 9999);
  uint32_t ageGga = (lastGgaMs ? (now - lastGgaMs) / 1000UL : 9999);
  uint32_t ageFix = (lastFixMs ? (now - lastFixMs) / 1000UL : 9999);
  (void)ageFix;

  // Re-armar salida NMEA si se silenció (>5s sin NMEA)
  if (ageNmea > 5 && !at.active) {
    Serial.println("[GNSSDIAG] No NMEA >5s -> re-enable NMEA 1Hz");
    (void)atRun("+CGPSNMEA=200191", "OK", "ERROR", 1200);
    (void)atRun("+CGPSNMEARATE=1", "OK", "ERROR", 1200);
  }

  // Si >15s sin NMEA, asegura GNSS ON (idempotente; barato)
  if (ageNmea > 15 && !at.active) {
    (void)atRun("+CGNSSPWR=1", "OK", "ERROR", 1200);
    (void)atRun("+CGPS=1", "OK", "ERROR", 3000);
  }

  // Imprimir cada 5s un resumen legible (solo Serial)
  static uint32_t nextPrint = 0;
  if (now >= nextPrint) {
    nextPrint = now + 5000;
    Serial.printf("[GNSSDIAG] rate=%u Hz  ageNMEA=%lus  ageGGA=%lus  fixQ=%u  "
                  "sats=%s  HDOP=%s  status=%s\n",
                  nmeaRate, (unsigned long)ageNmea, (unsigned long)ageGga,
                  (unsigned)fixQLast, satellitesStr.c_str(), hdopStr.c_str(),
                  gpsStatus.c_str());

    // Pequeño resumen de GSA/GSV por Serial
    if (!isnan(gsaPdop) || !isnan(gsaHdop2)) {
      Serial.printf("[GSA] fixType=%u used=%u PDOP=%.1f HDOP=%.1f VDOP=%.1f\n",
                    gsaFixType, gsaSatsUsed, gsaPdop, gsaHdop2, gsaVdop);
    }
    Serial.printf("[GSV] inView=%u SNR(avg)=%.1f SNR(max)=%.1f age=%lus\n",
                  (unsigned)gsvSatsInView, gsvSnrAvg, gsvSnrMax,
                  (unsigned)((gsvLastMs ? now - gsvLastMs : 0) / 1000UL));
  }

  // TTFF (solo una vez por ciclo de encendido/restart)
  if (!ttffPrinted && haveFix && lastFixMs && gnssStartMs) {
    uint32_t ttff =
        (lastFixMs >= gnssStartMs) ? (lastFixMs - gnssStartMs) / 1000UL : 0;
    Serial.printf("[GNSS] TTFF = %lu s\n", (unsigned long)ttff);
    ttffPrinted = true;
  }
}

// Ejecuta consultas AT de debug GNSS en máquina de estados no bloqueante.
// Permite inspección continua sin romper timing del firmware.
void gnssDebugPollAsync() {
  if (!config.gnssEnabled)
    return;
  const uint32_t PERIOD_MS = 5000;
  if (millis() < gnssDbgNextAt)
    return;
  if (at.active)
    return;

  bool done = false, ok = false;
  (void)atTick(done, ok);

  switch (gnssDbgState) {
  case GNSS_DBG_IDLE:
    gnssDbgState = GNSS_DBG_ASK_STATUS;
    break;
  case GNSS_DBG_ASK_STATUS:
    atBegin("+CGPSSTATUS?", "OK", "ERROR", 1200);
    gnssDbgState = GNSS_DBG_WAIT_STATUS;
    break;
  case GNSS_DBG_WAIT_STATUS:
    if ((bool)atTick(done, ok)) {
      Serial.println(ok ? "[GNSS] +CGPSSTATUS? OK"
                        : "[GNSS] +CGPSSTATUS? FAIL");
      Serial.print(at.resp);
      gnssDbgState = GNSS_DBG_ASK_INFO;
    }
    break;
  case GNSS_DBG_ASK_INFO:
    atBegin("+CGPSINFO", "OK", "ERROR", 1200);
    gnssDbgState = GNSS_DBG_WAIT_INFO;
    break;
  case GNSS_DBG_WAIT_INFO:
    if ((bool)atTick(done, ok)) {
      Serial.println(ok ? "[GNSS] +CGPSINFO OK" : "[GNSS] +CGPSINFO FAIL");
      Serial.print(at.resp);
      Serial.printf(
          "[GNSS] Status=%s Sats=%s HDOP=%s Alt=%sm Spd=%skm/h Lat=%s Lon=%s\n",
          gpsStatus.c_str(), satellitesStr.c_str(), hdopStr.c_str(),
          gpsAlt.c_str(), gpsSpeedKmh.c_str(), gpsLat.c_str(), gpsLon.c_str());
      gnssDbgState = GNSS_DBG_IDLE;
      gnssDbgNextAt = millis() + PERIOD_MS;
    }
    break;
  }
}

// -------------------- XTRA / AGNSS --------------------
// Detecta soporte XTRA (A-GNSS) y lo habilita en el módem.
// Mejora tiempo de primer fix cuando la red lo permite.
bool detectAndEnableXtra() {
  if (!config.gnssEnabled)
    return false;
  String r;
  if (!sendAtSync("+CGPSXE=?", r, 2000)) {
    Serial.println("[XTRA] Not supported");
    return false;
  }
  if (!sendAtSync("+CGPSXE=1", r, 2000)) {
    Serial.println("[XTRA] Enable FAIL");
    return false;
  }
  Serial.println("[XTRA] Enabled");
  return true;
}

// Descarga paquete XTRA una vez, asegurando PDP activo.
// Guarda estado para trazabilidad y decisiones futuras.
bool downloadXtraOnce() {
  if (!modem.isGprsConnected()) {
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      Serial.println("[XTRA] PDP reconnect FAIL");
      return false;
    }
  }
  String r;
  (void)sendAtSync("+CGPSXD=?", r, 2000);
  bool ok = sendAtSync("+CGPSXD=1", r, 120000);
  Serial.println(ok ? "[XTRA] Download OK" : "[XTRA] Download FAIL");
  xtraLastOk = ok;
  return ok;
}

// Refresca XTRA de forma periódica según ventana configurada.
// Evita descargas innecesarias y conserva recursos de red.
void downloadXtraIfDue() {
  if (!config.gnssEnabled)
    return;
  if (!xtraSupported)
    return;
  if (millis() - lastXtraDownload < XTRA_REFRESH_MS)
    return;
  if (downloadXtraOnce())
    lastXtraDownload = millis();
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\helpers.ino"
// -------------------- Safe value helpers --------------------
String missingUrlValue() { return "-1"; }

String safeFloatStr(float v) {
  if (isnan(v) || isinf(v))
    return missingUrlValue();
  return String(v, 3);
}

String safeUIntStr(uint32_t v) { return String(v); }

String safeIntStr(int v) { return String(v); }

String safeNumberStr(const String &s) {
  if (s.length() == 0)
    return missingUrlValue();
  if (s == "NaN" || s == "N/A")
    return missingUrlValue();

  bool hasDigit = false;
  bool hasDot = false;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (isDigit(c)) {
      hasDigit = true;
      continue;
    }
    if (c == '.' && !hasDot) {
      hasDot = true;
      continue;
    }
    if (c == '-' && i == 0)
      continue;
    return missingUrlValue();
  }
  return hasDigit ? s : missingUrlValue();
}

String safeGpsStr(const String &s) {
  if (s.length() == 0)
    return missingUrlValue();
  if (s == "NaN" || s == "N/A")
    return missingUrlValue();
  return s;
}

String safeSatsStr(const String &s) {
  if (s.length() == 0)
    return missingUrlValue();
  for (size_t i = 0; i < s.length(); ++i) {
    if (!isDigit(s[i]))
      return missingUrlValue();
  }
  return s;
}

// -------------------- Watchdog & Error Logging Helpers --------------------
void checkRebootReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
  case ESP_RST_POWERON:
    rebootReason = "PowerOn";
    break;
  case ESP_RST_SW:
    rebootReason = "Software";
    break;
  case ESP_RST_PANIC:
    rebootReason = "Panic";
    break;
  case ESP_RST_INT_WDT:
    rebootReason = "IntWatchdog";
    break;
  case ESP_RST_TASK_WDT:
    rebootReason = "TaskWatchdog";
    break;
  case ESP_RST_WDT:
    rebootReason = "OtherWatchdog";
    break;
  case ESP_RST_DEEPSLEEP:
    rebootReason = "DeepSleep";
    break;
  case ESP_RST_BROWNOUT:
    rebootReason = "Brownout";
    break;
  default:
    rebootReason = "Unknown";
    break;
  }

  Serial.printf("[RESET] Reason: %s (%d)\n", rebootReason.c_str(), (int)reason);
  if (previousResetStageValid) {
    Serial.printf("[RESET] Previous stage before reset: %s\n", previousResetStage);
  } else {
    Serial.println("[RESET] Previous stage before reset: unavailable");
  }
}

// -------------------- Error Logging --------------------
void logError(const String &type, const String &ctx, const String &msg) {
  static String lastType = "";
  static String lastCtx = "";
  static uint32_t lastLogMs = 0;
  static uint8_t suppressedCount = 0;
  const uint32_t RATE_LIMIT_MS = 5000;

  uint32_t now = millis();
  if (type == lastType && ctx == lastCtx && (now - lastLogMs) < RATE_LIMIT_MS) {
    if (suppressedCount < 255) {
      suppressedCount++;
    }
    return;
  }

  if (suppressedCount > 0 && SDOK) {
    File sf = SD.open(logFilePath, FILE_APPEND);
    if (sf) {
      if (sf.size() == 0) {
        sf.println("timestamp,type,context,message");
      }
      String ts = rtcOK ? rtc.now().timestamp() : String(now);
      sf.print(ts);
      sf.print(",");
      sf.print(lastType);
      sf.print(",");
      sf.print(lastCtx);
      sf.print(",");
      sf.println("suppressed=" + String(suppressedCount));
      sf.close();
    }
    suppressedCount = 0;
  }

  lastType = type;
  lastCtx = ctx;
  lastLogMs = now;
  Serial.println("[ERROR] " + type + " (" + ctx + "): " + msg);

  if (!SDOK)
    return;

  File f = SD.open(logFilePath, FILE_APPEND);
  if (f) {
    if (f.size() == 0) {
      f.println("timestamp,type,context,message");
    }
    String ts = rtcOK ? rtc.now().timestamp() : String(millis());
    f.print(ts);
    f.print(",");
    f.print(type);
    f.print(",");
    f.print(ctx);
    f.print(",");
    f.println(msg);
    f.close();
  }
}

// -------------------- HELPER FUNCTIONS FOR UI INFO SCREENS --------------------

// SyncRtcFromModem is in rtc.ino
// bool syncRtcFromModem() { ... }

// Get Network Time String for Display
String getNetworkTime() {
  String resp;
  if (sendAtSync("+CCLK?", resp, 2000)) {
    int idx = resp.indexOf("+CCLK: \"");
    if (idx >= 0) {
       // Return "hh:mm:ss dd/mm"
       // Correction: +CCLK: "23/05/12,14:20:00+00"
       String raw = resp.substring(idx + 8, idx + 25);
       // raw: yy/mm/dd,hh:mm:ss
       String timeS = raw.substring(9, 17); // hh:mm:ss
       String dateS = raw.substring(6, 8) + "/" + raw.substring(3, 5); // dd/mm
       return timeS + " " + dateS;
    }
  }
  return "--:--:-- --/--";
}

// Get Current CSV File Size
uint32_t getCsvFileSize() {
  if (!SDOK || csvFileName.length() == 0) return 0;
  if (!SD.exists(csvFileName)) return 0;
  
  File f = SD.open(csvFileName, FILE_READ);
  if (!f) return 0;
  uint32_t s = f.size();
  f.close();
  return s;
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\http.ino"
// -------------------- HTTP helpers --------------------
#include "config.h"
// -------------------- External Variables --------------------
extern TinyGsm modem;
extern struct AtSession at;
extern Adafruit_NeoPixel pixels;
extern const char apn[];
extern const char gprsUser[];
extern const char gprsPass[];
extern bool hasRed;
extern float batV;
extern uint16_t PM25;
extern SystemConfig config;
extern char currentCriticalStage[32];
extern void updatePmLed(float pm25);
extern void logError(const String &type, const String &ctx, const String &msg);
extern bool atTick(bool &done, bool &ok);
extern bool atRun(const String &cmd, const String &expect1,
                  const String &expect2, uint32_t timeout_ms);
extern bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms);

// Variables for PDP reconnect (kept local static as they are implementation
// details)
static uint8_t pdpReconnectFailCount = 0;
static uint32_t lastPdpReconnectAttempt = 0;
const uint8_t MAX_PDP_FAILS_BEFORE_BACKOFF = 5;
const uint32_t PDP_BACKOFF_MS = 15000;
const uint32_t PDP_RECONNECT_TIMEOUT_MS = 30000;

// Asegura sesión de datos PDP/NETOPEN activa antes de enviar HTTP.
// Incluye control de backoff para evitar bucles de reconexión agresivos.
bool ensurePdpAndNet() {
  String dummy;
  String cgdcont = String("+CGDCONT=1,\"IP\",\"") + String(apn) + "\"";
  (void)sendAtSync(cgdcont, dummy, 2000);

  if (!modem.isGprsConnected()) {
    // PROTECCIÓN CONTRA BLOQUEOS: Si hemos fallado muchas veces, esperar antes
    // de reintentar Esto evita bloqueos cuando se viaja entre redes celulares o
    // en zonas sin cobertura
    if (pdpReconnectFailCount >= MAX_PDP_FAILS_BEFORE_BACKOFF) {
      uint32_t timeSinceLastAttempt = millis() - lastPdpReconnectAttempt;
      if (timeSinceLastAttempt < PDP_BACKOFF_MS) {
        uint32_t remainingBackoff = PDP_BACKOFF_MS - timeSinceLastAttempt;
        Serial.printf(
            "[NET] Too many failures (%d), waiting %lu ms before retry\n",
            pdpReconnectFailCount, remainingBackoff);
        return false;
      } else {
        // Han pasado 15s, resetear contador y reintentar
        Serial.println("[NET] Backoff period over, resetting fail counter");
        pdpReconnectFailCount = 0;
      }
    }

    Serial.println("[NET] PDP down, reconnecting...");
    strncpy(currentCriticalStage, "http_pdp_reconnect", 31);
    currentCriticalStage[31] = '\0';
    lastPdpReconnectAttempt = millis();

    // MINI-LOOP CON WATCHDOG RESET: modem.gprsConnect() puede bloquear 10-60s
    // Alimentamos el watchdog cada 1s para evitar reset del ESP32
    uint32_t reconStart = millis();
    bool reconOk = false;
    while (millis() - reconStart < PDP_RECONNECT_TIMEOUT_MS) {
      esp_task_wdt_reset(); // Evitar watchdog timeout cada 1s

      if (modem.gprsConnect(apn, gprsUser, gprsPass)) {
        reconOk = true;
        break;
      }

      delay(1000); // Esperar 1s entre intentos internos
    }

    if (!reconOk) {
      pdpReconnectFailCount++;
      Serial.printf(
          "[NET] PDP reconnect FAIL after %lu ms (fail count: %d/%d)\n",
          PDP_RECONNECT_TIMEOUT_MS, pdpReconnectFailCount,
          MAX_PDP_FAILS_BEFORE_BACKOFF);
      hasRed = false;
      return false;
    }

    // Éxito: resetear contador de fallos
    pdpReconnectFailCount = 0;
    hasRed = true;
    Serial.println("[NET] PDP reconnected OK");
  }

  String r;
  strncpy(currentCriticalStage, "http_netopen", 31);
  currentCriticalStage[31] = '\0';
  if (!sendAtSync("+NETOPEN?", r, 2000) || r.indexOf("+NETOPEN: 1") < 0) {
    if (!sendAtSync("+NETOPEN", r, 10000)) {
      Serial.println("[NET] NETOPEN FAIL");
      hasRed = false;
      return false;
    }
  }
  hasRed = true;
  return true;
}

// Helper: parsear +HTTPACTION: 0,200,123
// Parsea la URC +HTTPACTION para extraer código HTTP y tamaño de respuesta.
// Centraliza parsing defensivo del módem SIM7600.
void parseHttpActionResponse(const String &resp, int &code, int &dataLen) {
  code = -1;
  dataLen = -1;
  int p = resp.indexOf("+HTTPACTION:");
  int c1 = resp.indexOf(',', p);
  int c2 = resp.indexOf(',', c1 + 1);
  if (p >= 0 && c1 > 0 && c2 > c1) {
    code = resp.substring(c1 + 1, c2).toInt();
    dataLen = resp.substring(c2 + 1).toInt();
  }
}

// -------------------- HTTP GET (blocking, stable) --------------------
// Ejecuta ciclo HTTP completo (INIT/PARA/ACTION/READ/TERM) con timeout y watchdog.
// Devuelve true solo para respuestas 2xx y registra errores detallados.
bool httpGet_webhook(const String &fullUrl) {
  Serial.printf("[HTTP][SYNC] URL length = %d\n", fullUrl.length());
  if (fullUrl.length() > 512) {
    Serial.println("[HTTP][WARN] URL >512 chars; SIM7600 +HTTPPARA may fail.");
  }

  if (!ensurePdpAndNet()) {
    logError("HTTP_PDP_FAIL", "ensurePdpAndNet", "PDP/NET setup failed");
    return false;
  }

  bool done = false, ok = false;
  (void)atTick(done, ok);

  (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
  if (!atRun("+HTTPINIT", "OK", "ERROR", 5000)) {
    Serial.println("[HTTP][ERR] HTTPINIT FAIL");
    logError("HTTP_INIT_FAIL", "HTTPINIT", at.resp);
    return false;
  }
  if (!atRun("+HTTPPARA=\"CID\",1", "OK", "ERROR", 2000)) {
    Serial.println("[HTTP][ERR] HTTPPARA CID FAIL");
    logError("HTTP_CID_FAIL", "HTTPPARA_CID", at.resp);
    (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
    return false;
  }
  {
    String cmd = "+HTTPPARA=\"URL\",\"" + fullUrl + "\"";
    if (!atRun(cmd, "OK", "ERROR", 6000)) {
      Serial.println("[HTTP][ERR] Set URL FAIL");
      logError("HTTP_URL_FAIL", "HTTPPARA_URL", at.resp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      return false;
    }
  }

  // Apagar LED para ahorrar energía durante transmisión
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  atBegin("+HTTPACTION=0", "+HTTPACTION:", "ERROR", 90000);
  strncpy(currentCriticalStage, "http_action", 31);
  currentCriticalStage[31] = '\0';
  int httpCode = -1, dataLen = -1;
  {
    bool actionDone = false, actionOk = false;
    uint32_t startTime = millis();
    // Timeout HTTP configurable (config.httpTimeout en segundos)
    // fallback de seguridad: 15s si la config viene en 0.
    const uint32_t MAX_HTTP_WAIT_MS =
        (config.httpTimeout > 0 ? (uint32_t)config.httpTimeout * 1000UL : 15000UL);

    while (!actionDone) {
      esp_task_wdt_reset(); // Reset watchdog para evitar timeout durante HTTP

      if (atTick(actionDone, actionOk))
        break;

      // Check timeout
      if (millis() - startTime > MAX_HTTP_WAIT_MS) {
        uint32_t elapsed = millis() - startTime;
        Serial.printf("[HTTP][TIMEOUT] After %lu ms (bat=%.2fV)\n", elapsed,
                      batV);
        logError("HTTP_TIMEOUT", "HTTPACTION",
                 "Timeout=" + String(elapsed) + "ms bat=" + String(batV, 2) +
                     "V");
        (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
        updatePmLed((float)PM25);
        return false;
      }

      delay(1);
    }
    String actionResp = at.resp;
    if (!actionOk) {
      Serial.println("[HTTP][ERR] +HTTPACTION did not complete");
      logError("HTTP_ACTION_FAIL", "HTTPACTION", actionResp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      // Restaurar LED antes de salir
      updatePmLed((float)PM25);
      return false;
    }
    parseHttpActionResponse(actionResp, httpCode, dataLen);
    Serial.printf("[HTTP] code=%d len=%d\n", httpCode, dataLen);
    if (httpCode == -1) {
      Serial.println("[HTTP][ERR] Could not parse +HTTPACTION");
      logError("HTTP_PARSE_FAIL", "HTTPACTION", actionResp);
    }
  }

  if (dataLen > 0) {
    String cmd = "+HTTPREAD=0," + String(dataLen);
    String readResp;
    if (sendAtSync(cmd, readResp, 8000)) {
      Serial.println("[HTTP][READ] ----------------");
      Serial.println(readResp);
      Serial.println("[HTTP][READ] ----------------");
    } else {
      Serial.println("[HTTP][WARN] HTTPREAD failed");
    }
  }

  (void)atRun("+HTTPTERM", "OK", "ERROR", 2000);

  // Restaurar LED según nivel de PM2.5
  updatePmLed((float)PM25);

  return (httpCode >= 200 && httpCode < 300);
}


#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\rtc.ino"
// -------------------- Debug --------------------
bool syncRtcFromModem(); // Forward declaration
void printRtcOnce() {
  if (!rtcOK)
    return;
  DateTime now = rtc.now();
  Serial.printf("[RTC] %04d/%02d/%02d %02d:%02d:%02d epoch=%lu\n", now.year(),
                now.month(), now.day(), now.hour(), now.minute(), now.second(),
                (unsigned long)now.unixtime());
}
static uint8_t monthFromAbbrev(const char *m) {
  if (!m)
    return 1;
  if (!strncmp(m, "Jan", 3))
    return 1;
  if (!strncmp(m, "Feb", 3))
    return 2;
  if (!strncmp(m, "Mar", 3))
    return 3;
  if (!strncmp(m, "Apr", 3))
    return 4;
  if (!strncmp(m, "May", 3))
    return 5;
  if (!strncmp(m, "Jun", 3))
    return 6;
  if (!strncmp(m, "Jul", 3))
    return 7;
  if (!strncmp(m, "Aug", 3))
    return 8;
  if (!strncmp(m, "Sep", 3))
    return 9;
  if (!strncmp(m, "Oct", 3))
    return 10;
  if (!strncmp(m, "Nov", 3))
    return 11;
  if (!strncmp(m, "Dec", 3))
    return 12;
  return 1;
}

// Epoch de tiempo de compilación (__DATE__ y __TIME__)
static uint32_t compileUnixTime() {
  const char *d = __DATE__; // "Mmm dd yyyy"
  const char *t = __TIME__; // "HH:MM:SS"

  char mon[4] = {0};
  int day = 1, year = 2000, hh = 0, mm = 0, ss = 0;

  if (sscanf(d, "%3s %d %d", mon, &day, &year) != 3) {
    return MIN_VALID_EPOCH;
  }
  (void)sscanf(t, "%d:%d:%d", &hh, &mm, &ss);

  struct tm tmv = {};
  tmv.tm_year = year - 1900;
  tmv.tm_mon = monthFromAbbrev(mon) - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = hh;
  tmv.tm_min = mm;
  tmv.tm_sec = ss;
  return (uint32_t)mktime(&tmv);
}

// -------------------- Modem Time --------------------

bool getModemEpoch(uint32_t &epoch_out) {
  String resp;
  if (!sendAtSync("+CCLK?", resp, 2000))
    return false;

  int q1 = resp.indexOf('"');
  int q2 = resp.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0)
    return false;

  String ts = resp.substring(q1 + 1, q2); // ej: "25/07/30,14:22:10+08"

  int s1 = ts.indexOf('/');
  int s2 = ts.indexOf('/', s1 + 1);
  int c = ts.indexOf(',');
  int k1 = ts.indexOf(':', c);
  int k2 = ts.indexOf(':', k1 + 1);
  if (s1 < 0 || s2 < 0 || c < 0 || k1 < 0 || k2 < 0)
    return false;

  int yy = ts.substring(0, s1).toInt() + 2000;
  int mo = ts.substring(s1 + 1, s2).toInt();
  int dd = ts.substring(s2 + 1, c).toInt();
  int hh = ts.substring(c + 1, k1).toInt();
  int mm = ts.substring(k1 + 1, k2).toInt();
  int ss = ts.substring(k2 + 1, k2 + 3).toInt();

  struct tm t = {};
  t.tm_year = yy - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = dd;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  time_t e = mktime(&t);
  if (e < MIN_VALID_EPOCH)
    return false;

  epoch_out = (uint32_t)e;
  return true;
}

// -------------------- Smart Sync --------------------
void syncRtcSmart() {
  if (!rtc.begin()) {
    Serial.println("[RTC] Not found");
    rtcOK = false;
    return;
  }
  rtcOK = true;

  // Cargar contador de syncs desde Preferences
  prefs.begin("rtc", false);
  rtcModemSyncCount = prefs.getUChar("syncCnt", 0);
  prefs.end();
  Serial.printf("[RTC] Modem sync count: %u/%u\n", rtcModemSyncCount,
                MAX_MODEM_SYNC_COUNT);

  // Obtener hora actual del RTC
  DateTime rtcNow = rtc.now();
  uint32_t rtcEpoch = rtcNow.unixtime();

  // Solo usar compile time si RTC perdió potencia o tiene hora inválida
  if (rtc.lostPower() || rtcEpoch < MIN_VALID_EPOCH) {
    uint32_t ctEpoch = compileUnixTime();
    rtc.adjust(DateTime(ctEpoch));
    Serial.println(
        "[RTC] Initialized with compile time (lost power or invalid)");
    Serial.printf("[RTC] Compile time: %lu\n", (unsigned long)ctEpoch);
  } else {
    Serial.printf(
        "[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d (epoch=%lu)\n",
        rtcNow.year(), rtcNow.month(), rtcNow.day(), rtcNow.hour(),
        rtcNow.minute(), rtcNow.second(), (unsigned long)rtcEpoch);
  }

  // Intentar sincronizar con modem (solo si no alcanzó límite)
  if (rtcModemSyncCount < MAX_MODEM_SYNC_COUNT) {
    syncRtcFromModem();
  } else {
    Serial.println("[RTC] Max modem sync count reached, skipping network sync");
    rtcNetSyncPending = false;
  }
}

// -------------------- Reset Modem Sync Counter --------------------
// Resetea el contador de sincronizaciones con el modem
// Útil si quieres forzar nuevas sincronizaciones después de alcanzar el límite
void resetModemSyncCounter() {
  rtcModemSyncCount = 0;
  prefs.begin("rtc", false);
  prefs.putUChar("syncCnt", 0);
  prefs.end();
  Serial.println("[RTC] Modem sync counter reset to 0");
}

// -------------------- Sync from Modem (llamable manualmente)
// -------------------- Esta función sincroniza el RTC con la hora del modem
// celular Puede ser llamada manualmente o desde syncRtcSmart() Retorna true si
// la sincronización fue exitosa
bool syncRtcFromModem() {
  if (!rtcOK) {
    Serial.println("[RTC] Cannot sync: RTC not available");
    return false;
  }

  // Habilitar actualización automática de zona horaria
  (void)atRun("+CTZU=1", "OK", "ERROR", 1000);
  (void)atRun("+CTZR=1", "OK", "ERROR", 1000);

  uint32_t modemEpoch = 0;
  bool haveModem = getModemEpoch(modemEpoch);

  if (!haveModem) {
    Serial.println("[RTC] Modem time not ready; will retry in loop");
    rtcNetSyncPending = true;
    rtcNextProbeMs = millis() + RTC_PROBE_PERIOD_MS;
    return false;
  }

  // Comparar con RTC actual
  uint32_t rtcEpoch = rtc.now().unixtime();
  long diff = (long)modemEpoch - (long)rtcEpoch;

  Serial.printf("[RTC] rtc=%lu modem=%lu diff=%ld s\n", (unsigned long)rtcEpoch,
                (unsigned long)modemEpoch, diff);

  // Solo sincronizar si la diferencia supera el umbral
  if (abs(diff) > RTC_SYNC_THRESHOLD) {
    rtc.adjust(DateTime(modemEpoch));
    Serial.println("[RTC] Synchronized to modem clock");

    // Incrementar y guardar contador
    rtcModemSyncCount++;
    prefs.begin("rtc", false);
    prefs.putUChar("syncCnt", rtcModemSyncCount);
    prefs.end();
    Serial.printf("[RTC] Sync count updated: %u/%u\n", rtcModemSyncCount,
                  MAX_MODEM_SYNC_COUNT);

    rtcNetSyncPending = false;
    return true;
  } else {
    Serial.println("[RTC] Within threshold; no sync needed");
    rtcNetSyncPending = false;
    return false;
  }
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\sd_card.ino"
// -------------------- SD helpers --------------------
#include "config.h"
extern SystemConfig config;
extern bool SHT4xOK, GasOK, ENS160OK;
extern float tempsht4x, humsht4x, gasPpm, pmsTempC, pmsHum, rtcTempC, batV;
extern uint16_t PM1, PM25, PM10;
extern int SDS198PM100;
extern String gpsDate, gpsLat, gpsLon, gpsAlt, gpsSpeedKmh, satellitesStr, hdopStr, rebootReason;
extern bool rtcOK, SDOK, loggingEnabled, xtraLastOk;
extern uint32_t sdSaveCounter, sendCounter;
extern String csvFileName, logFilePath;
extern uint8_t lastDayLogged;
extern uint16_t ens160Tvoc, ens160Eco2;
extern uint8_t ens160Aqi;
extern bool ens160DataValid;
// Genera nombre diario de CSV usando prefijo de dispositivo + fecha RTC.
// Permite rotación por día y continuidad de trazabilidad en terreno.
String generateCSVFileName() {
  // Genera nombre basado en DEVICE_ID_STR y fecha actual
  // Formato: hiripro<ID>_DD_MM_YYYY.csv
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  char dateStr[16];
  snprintf(dateStr, sizeof(dateStr), "%02d_%02d_%04d", now.day(), now.month(),
           now.year());

  String name = "/hiripro" + String(DEVICE_ID_STR) + "_" + String(dateStr) + ".csv";
  Serial.print("[SD] CSV filename: ");
  Serial.println(name);
  return name;
}

// Escribe cabecera CSV solo si el archivo está vacío.
// Estandariza columnas para procesamiento posterior en backend/IA.
void writeCSVHeader() {
  if (!SDOK)
    return;
  File f = SD.open(csvFileName, FILE_APPEND);
  if (f) {
    if (f.size() == 0) { // Only write header if file is empty
      f.println("ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,"
                "pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht4xTempC,"
                "sht4xHum,resetReason,pm100,"
                "gasPpm,tvoc,eco2,aqi,notas");
      Serial.println("[SD] Wrote header to " + csvFileName);
    }
    f.close();
  } else {
    Serial.println("[SD][ERR] Failed to open " + csvFileName +
                   " to write header.");
  }
}

// Crea archivo de errores con esquema fijo si aún no existe.
// Facilita auditoría de fallos de red/módem en campo.
void writeErrorLogHeader() {
  if (!SDOK)
    return;
  if (SD.exists(logFilePath.c_str()))
    return; // Ya existe
  File f = SD.open(logFilePath.c_str(), FILE_WRITE);
  if (f) {
    f.println("timestamp,type,context,message");
    f.close();
    Serial.println("[SD] Error log header created");
  }
}

// Variable global para almacenar la última línea guardada (para visualización
// en OLED)
extern String lastSavedCSVLine;

extern String currentNote;

// Guarda una muestra completa de telemetría en SD y rota por cambio de día.
// Actualiza contadores y expone última línea guardada para UI/debug.
bool saveCSVData() {
  if (!SDOK || !loggingEnabled)
    return false;
  DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);

  // Detectar cambio de día (muy eficiente: 1 ciclo CPU)
  if (now.day() != lastDayLogged) {
    Serial.println("[SD] Day changed, creating new file...");
    csvFileName = generateCSVFileName();
    writeCSVHeader();
    lastDayLogged = now.day();

    // Persist new filename
    prefs.begin("system", false);
    prefs.putString("csvFile", csvFileName);
    prefs.end();
  }

  char hhmmss[9];
  snprintf(hhmmss, sizeof(hhmmss), "%02d:%02d:%02d", now.hour(), now.minute(),
           now.second());
  String line = String(millis()) + "," + hhmmss + "," + gpsDate + "," + gpsLat +
                "," + gpsLon + "," + gpsAlt + "," + gpsSpeedKmh + "," +
                String(PM1) + "," + String(PM25) + "," + String(PM10) + "," +
                (isnan(pmsTempC) ? "0" : String(pmsTempC, 1)) + "," +
                (isnan(pmsHum) ? "0" : String(pmsHum, 1)) + "," +
                String(rtcTempC, 2) + "," + String(batV, 2) + "," +
                String(csq) + "," + satellitesStr + "," + hdopStr + "," +
                (xtraLastOk ? "1" : "0") + "," + String(tempsht4x, 2) + "," + 
                String(humsht4x, 2) + "," + rebootReason + "," + 
                String(SDS198PM100) + "," +
                ((GasOK && !isnan(gasPpm)) ? String(gasPpm, 2) : "0") + "," +
                ((ENS160OK && ens160DataValid) ? String(ens160Tvoc) : "0") + "," +
                ((ENS160OK && ens160DataValid) ? String(ens160Eco2) : "0") + "," +
                ((ENS160OK && ens160DataValid) ? String(ens160Aqi) : "0") + "," +
                currentNote;

  File f = SD.open(csvFileName, FILE_APPEND);
  if (f) {
    f.println(line);
    f.close();
    sdSaveCounter++;         // Incrementar contador de guardados exitosos en SD
    lastSavedCSVLine = line; // Guardar línea para visualización en OLED
    Serial.println(String("[SD] Saved line: ") + line);
    
    // Clear one-shot note after writing
    currentNote = ""; 
    
    return true;
  } else {
    Serial.println("[SD][ERR] Failed to open " + csvFileName +
                   " for appending.");
    return false;
  }
}

// -------------------- Failed Transmission Log (Debug only)
// -------------------- Esta función guarda las transmisiones HTTP fallidas para
// análisis posterior NO se reintenta la transmisión automáticamente para evitar
// desfase de datos Variables globales ajustables para pruebas:
// - failedTxPath: Ruta del archivo CSV (definido en .ino principal)
// Registra en SD las transmisiones HTTP fallidas con timestamp y URL.
// No reintenta en línea para evitar desfases y preservar ciclo de muestreo.
void saveFailedTransmission(const String &url, const String &errorType) {
  if (!SDOK)
    return; // SD no disponible

  DateTime now = rtc.now();
  char timestamp[20];
  snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(),
           now.second());

  // Verificar si necesitamos crear el header (primera vez)
  bool needsHeader = !SD.exists(failedTxPath.c_str());

  File f = SD.open(failedTxPath.c_str(), FILE_APPEND);
  if (f) {
    // Escribir header si es la primera línea
    if (needsHeader) {
      f.println("timestamp,error_type,url");
      Serial.println("[FAILED_TX] Created header in " + failedTxPath);
    }

    // Escribir línea de fallo
    // Formato: timestamp,error_type,url (URL entre comillas por si tiene comas)
    String line = String(timestamp) + "," + errorType + ",\"" + url + "\"";
    f.println(line);
    f.close();

    Serial.println("[FAILED_TX] Logged: " + errorType);
  } else {
    Serial.println("[FAILED_TX][ERR] Could not open " + failedTxPath);
  }
}


#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\serial_commands.ino"
// -------------------- Serial Commands --------------------
// Sistema de comandos seriales para debug y mantenimiento
// Uso: Enviar comando por serial monitor (115200 baud, newline)
// Ejemplo: "help" → muestra lista de comandos

#include "config.h"

extern SystemConfig config;
extern TinyGsm modem;
extern RTC_DS3231 rtc;
extern Preferences prefs;
extern bool rtcOK;
extern bool SDOK;
extern bool loggingEnabled;
extern bool streaming;
extern bool oledOK;
extern uint32_t sendCounter;
extern uint32_t sdSaveCounter;
extern String csvFileName;
extern String rebootReason;
extern String networkOperator;
extern String networkTech;
extern String signalQuality;
extern String registrationStatus;
extern float batV;
extern int csq;
extern String gpsStatus;
extern String satellitesStr;
extern String hdopStr;
extern const char *DEVICE_ID_STR;
extern String VERSION;
extern String logFilePath;
extern String failedTxPath;

// Forward declarations
void saveConfig();
void configSetDefaults();
void printConfig();
void applyLEDConfig();
void clearSDCard();
void printSDInfo();
void printSDFileList();
String generateCSVFileName();
void writeCSVHeader();
bool syncRtcFromModem();      // Assuming implemented or needed
void resetModemSyncCounter(); // Assuming implemented or needed
bool getModemEpoch(uint32_t &epoch);

// Procesa comandos por puerto serie para operación, diagnóstico y mantenimiento.
// Permite inspección y control en campo sin recompilar firmware.
void processSerialCommand() {
  if (!Serial.available())
    return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0)
    return;

  Serial.println("\n>>> Command: " + cmd);

  // -------------------- HELP --------------------
  if (cmd == "help" || cmd == "?") {
    Serial.println(F("=== HIRI PRO Serial Commands ==="));
    Serial.println(F("\n[RTC/Time]"));
    Serial.println(F("  rtc         - Show current RTC time"));
    Serial.println(F("  rtcsync     - Force sync RTC with modem"));
    Serial.println(F("  modemtime   - Show modem time only"));

    Serial.println(F("\n[Counters]"));
    Serial.println(F("  counters    - Show SD/HTTP counters"));
    Serial.println(F("  resetcnt    - Reset both counters to 0"));
    Serial.println(F("  stats       - Show detailed statistics"));

    Serial.println(F("\n[SD Card]"));
    Serial.println(F("  sdinfo      - Show SD card info"));
    Serial.println(F("  sdlist      - List files on SD"));
    Serial.println(F("  sdnew       - Create new CSV file"));
    Serial.println(
        F("  sdclear     - Delete all files on SD (WARNING: irreversible!)"));

    Serial.println(F("\n[Network/Modem]"));
    Serial.println(F("  netinfo     - Show network info"));
    Serial.println(F("  csq         - Show signal quality"));

    Serial.println(F("\n[System]"));
    Serial.println(F("  sysinfo     - Show system info"));
    Serial.println(F("  reboot      - Reboot ESP32"));
    Serial.println(F("  mem         - Show memory usage"));

    Serial.println(F("\n[Streaming]"));
    Serial.println(F("  start       - Start streaming"));
    Serial.println(F("  stop        - Stop streaming"));

    Serial.println(F("\n[UI / OLED Flow]"));
    Serial.println(F("  BTN1: navegar / cancelar prompt"));
    Serial.println(F("  BTN2: seleccionar / confirmar prompt"));
    Serial.println(F("  Menu Mensajes -> guarda nota one-shot en CSV (columna notas)"));
    Serial.println(F("  Informacion > WIFI SD (ON/OFF): BTN2 entra/sale AP"));

    Serial.println(F("\n[Configuration]"));
    Serial.println(F("  config              - Show all configuration"));
    Serial.println(F("  config sd/http/display/power/system - Show specific config"));
    Serial.println(F("  set sdauto on/off   - Mount SD on boot"));
    Serial.println(F("  set sdsave 3/60/600/1200 - SD save period (seconds)"));
    Serial.println(F("  set httpsend 3/60/600/1200 - HTTP send period (seconds)"));
    Serial.println(F("  set httptimeout 5-30 - HTTP timeout (seconds)"));
    Serial.println(F("  set oledoff on/off  - OLED auto-off"));
    Serial.println(F("  set oledtime 60/120/180 - OLED timeout (seconds)"));
    Serial.println(F("  set led on/off      - Enable NeoPixel"));
    Serial.println(F("  set ledbright 10/25/50/100 - LED brightness (%)"));
    Serial.println(F("  set autostart on/off - Autostart streaming on boot"));
    Serial.println(F("  set gnss on/off     - Enable or disable GNSS subsystem"));
    Serial.println(F("  set autowaitgps on/off - Wait GPS fix before start"));
    Serial.println(
        F("  set autogpsto 60-900 - GPS timeout (60s-15min, default: 600s)"));
    Serial.println(F("  set autodebug on/off - Auto-start in debug mode"));
    Serial.println(F("  set rotatedisplay on/off - Rotate display 180 deg"));
    Serial.println(F("  set reboot 0-720   - Auto reboot hours (0 disables, default 3)"));
    Serial.println(
        F("  set gnssmode 1/3/5/7/15 - GNSS mode (1=GPS, 3=GPS+GLO, 15=ALL)"));
    Serial.println(F("  configreset         - Reset to defaults"));
    Serial.println(F("  configsave          - Save config to flash"));

    Serial.println(F("\n"));
  }

  // -------------------- RTC COMMANDS --------------------
  else if (cmd == "rtc") {
    if (!rtcOK) {
      Serial.println("[RTC] Not available");
      return;
    }
    DateTime now = rtc.now();
    Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(), now.hour(), now.minute(),
                  now.second());
    Serial.printf("[RTC] Epoch: %lu\n", (unsigned long)now.unixtime());
    Serial.printf("[RTC] Temperature: %.2f°C\n", rtc.getTemperature());
    // Serial.printf("[RTC] Modem syncs: %u/3\n", rtcModemSyncCount);
  }  
  else if (cmd == "rtcsync") {
    if (!rtcOK) {
      Serial.println("[RTC] Not available");
      return;
    }
     Serial.printf("[RTC] Modem syncs: %u/3\n", rtcModemSyncCount);
    if (syncRtcFromModem()) {
      Serial.println("[RTC] ✓ Synced from modem");
    } else {
      Serial.println("[RTC] ✗ Sync failed");
    }
   
  }
  else if (cmd == "modemtime") {
    uint32_t epoch = 0;
    if (getModemEpoch(epoch)) {
      DateTime dt(epoch);
      Serial.printf("[MODEM] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                    dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
    } else {
      Serial.println("[MODEM] ✗ Failed to get time");
    }
  }

  // -------------------- COUNTER COMMANDS --------------------
  else if (cmd == "counters") {
    Serial.println("=== Counters ===");
    Serial.printf("SD saves:     %lu\n", (unsigned long)sdSaveCounter);
    Serial.printf("HTTP success: %lu\n", (unsigned long)sendCounter);
    if (sdSaveCounter > 0) {
      float successRate = (float)sendCounter / (float)sdSaveCounter * 100.0f;
      Serial.printf("Success rate: %.1f%%\n", successRate);
    }
  }

  else if (cmd == "resetcnt") {
    Serial.println("[COUNTERS] Resetting to 0...");
    sendCounter = 0;
    sdSaveCounter = 0;
    prefs.begin("system", false);
    prefs.putUInt("sendCnt", 0);
    prefs.putUInt("sdCnt", 0);
    prefs.end();
    Serial.println("[COUNTERS] ✓ Reset complete");
  }

  else if (cmd == "stats") {
    Serial.println("=== System Statistics ===");
    Serial.printf("Version:      %s\n", VERSION.c_str());
    Serial.printf("Device ID:    %s\n", DEVICE_ID_STR);
    Serial.printf("Uptime:       %lu s\n", millis() / 1000);
    Serial.printf("Streaming:    %s\n", streaming ? "ON" : "OFF");
    Serial.printf("SD logging:   %s\n", loggingEnabled ? "ON" : "OFF");
    Serial.printf("SD saves:     %lu\n", (unsigned long)sdSaveCounter);
    Serial.printf("HTTP success: %lu\n", (unsigned long)sendCounter);
    if (sdSaveCounter > 0) {
      float successRate = (float)sendCounter / (float)sdSaveCounter * 100.0f;
      Serial.printf("Success rate: %.1f%%\n", successRate);
    }
    Serial.printf("Battery:      %.2fV\n", batV);
    Serial.printf("CSQ:          %d\n", csq);
    Serial.printf("GPS:          %s\n", gpsStatus.c_str());
    Serial.printf("Satellites:   %s\n", satellitesStr.c_str());
    Serial.printf("HDOP:         %s\n", hdopStr.c_str());
    Serial.printf("Reboot reason:%s\n", rebootReason.c_str());
  }

  // -------------------- SD COMMANDS --------------------
  else if (cmd == "sdinfo") {
    printSDInfo();
  }

  else if (cmd == "sdlist") {
    printSDFileList();
  }

  else if (cmd == "sdnew") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    Serial.println("[SD] Creating new CSV file...");
    csvFileName = generateCSVFileName();
    writeCSVHeader();
    Serial.printf("[SD] ✓ New file created: %s\n", csvFileName.c_str());
    // Persist new filename
    prefs.begin("system", false);
    prefs.putString("csvFile", csvFileName);
    prefs.end();
  }

  else if (cmd == "sdclear") {
    if (!SDOK) {
      Serial.println("[SD] Not available");
      return;
    }
    Serial.println(
        "[SD] ⚠ WARNING: This will delete ALL files on the SD card!");
    Serial.println("[SD] Type 'sdclear confirm' to proceed");
  }

  else if (cmd == "sdclear confirm") {
    clearSDCard();
  }

  // -------------------- NETWORK COMMANDS --------------------
  else if (cmd == "netinfo") {
    Serial.println("=== Network Info ===");
    Serial.printf("Operator:     %s\n", networkOperator.c_str());
    Serial.printf("Technology:   %s\n", networkTech.c_str());
    Serial.printf("CSQ:          %d\n", csq);
    Serial.printf("Registration: %s\n", registrationStatus.c_str());
    Serial.printf("PDP connected:%s\n", modem.isGprsConnected() ? "YES" : "NO");
    Serial.printf("Network conn: %s\n",
                  modem.isNetworkConnected() ? "YES" : "NO");
  }

  else if (cmd == "csq") {
    int newCsq = modem.getSignalQuality();
    Serial.printf("[MODEM] Signal Quality: %d", newCsq);
    if (newCsq == 0)
      Serial.println(" (no signal)");
    else if (newCsq < 10)
      Serial.println(" (marginal)");
    else if (newCsq < 15)
      Serial.println(" (ok)");
    else if (newCsq < 20)
      Serial.println(" (good)");
    else
      Serial.println(" (excellent)");
  }

  // -------------------- SYSTEM COMMANDS --------------------
  else if (cmd == "sysinfo") {
    Serial.println("=== System Info ===");
    Serial.printf("Firmware:     %s\n", VERSION.c_str());
    Serial.printf("Device ID:    %s\n", DEVICE_ID_STR);
    Serial.printf("Chip model:   %s\n", ESP.getChipModel());
    Serial.printf("Chip cores:   %d\n", ESP.getChipCores());
    Serial.printf("CPU freq:     %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash size:   %lu MB\n",
                  ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Free heap:    %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Uptime:       %lu s\n", millis() / 1000);
    if (config.scheduledRebootHours == 0) {
      Serial.println("Auto reboot:  OFF");
    } else {
      uint32_t uptimeS = millis() / 1000UL;
      uint32_t intervalS = (uint32_t)config.scheduledRebootHours * 3600UL;
      uint32_t remainingS = uptimeS < intervalS ? intervalS - uptimeS : 0;
      Serial.printf("Auto reboot:  every %u h, next in %lu s\n",
                    config.scheduledRebootHours, remainingS);
    }
    Serial.printf("Reboot reason:%s\n", rebootReason.c_str());
  }

  else if (cmd == "mem") {
    Serial.println("=== Memory Usage ===");
    Serial.printf("Free heap:    %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Heap size:    %lu bytes\n", ESP.getHeapSize());
    Serial.printf("Min free heap:%lu bytes\n", ESP.getMinFreeHeap());
    Serial.printf("Max alloc:    %lu bytes\n", ESP.getMaxAllocHeap());
  }

  else if (cmd == "reboot") {
    Serial.println("[SYSTEM] Rebooting in 2 seconds...");
    delay(2000);
    ESP.restart();
  }

  // -------------------- STREAMING COMMANDS --------------------
  else if (cmd == "start") {
    if (streaming) {
      Serial.println("[STREAM] Already running");
    } else {
      Serial.println("[STREAM] Starting via serial command...");
      streaming = true;
      // Etapa de integración: iniciar transmisión con logging OFF.
      loggingEnabled = false;
      Serial.printf("[STREAM] ✓ Started (SD logging: %s)\n",
                    loggingEnabled ? "ON" : "OFF");
    }
  }

  else if (cmd == "stop") {
    if (!streaming) {
      Serial.println("[STREAM] Already stopped");
    } else {
      Serial.println("[STREAM] Stopping...");
      streaming = false;
      loggingEnabled = false;
      Serial.println("[STREAM] ✓ Stopped");
    }
  }

  // -------------------- CONFIGURATION COMMANDS --------------------
  else if (cmd == "config") {
    printConfig();
  }

  else if (cmd == "config sd") {
    Serial.println("=== SD Configuration ===");
    Serial.printf("Auto-mount on boot: %s\n",
                  config.sdAutoMount ? "ON" : "OFF");
    Serial.printf("Save period:        %lu ms (%lu s)\n", config.sdSavePeriod,
                  config.sdSavePeriod / 1000);
  }

  else if (cmd == "config http") {
    Serial.println("=== HTTP Configuration ===");
    Serial.printf("Send period: %lu ms (%lu s)\n", config.httpSendPeriod,
                  config.httpSendPeriod / 1000);
    Serial.printf("Timeout:     %u seconds\n", config.httpTimeout);
  }

  else if (cmd == "config display") {
    Serial.println("=== Display Configuration ===");
    Serial.printf("Auto-off: %s\n", config.oledAutoOff ? "ON" : "OFF");
    Serial.printf("Timeout:  %lu ms (%lu s)\n", config.oledTimeout,
                  config.oledTimeout / 1000);
  }

  else if (cmd == "config power") {
    Serial.println("=== Power/LED Configuration ===");
    Serial.printf("NeoPixel enabled: %s\n", config.ledEnabled ? "YES" : "NO");
    Serial.printf("Brightness:       %u%%\n", config.ledBrightness);
  }

  else if (cmd == "config system") {
    Serial.println("=== System Configuration ===");
    Serial.printf("Rotate display:   %s\n", config.rotateDisplay ? "YES" : "NO");
    if (config.scheduledRebootHours == 0) {
      Serial.println("Scheduled reboot: OFF");
    } else {
      Serial.printf("Scheduled reboot: every %u hours\n",
                    config.scheduledRebootHours);
    }
  }

  else if (cmd.startsWith("set ")) {
    String param = cmd.substring(4);
    param.trim();

    // SD auto-mount
    if (param == "sdauto on") {
      config.sdAutoMount = true;
      saveConfig();
      Serial.println("[CONFIG] SD auto-mount: ON (will mount on next boot)");
    } else if (param == "sdauto off") {
      config.sdAutoMount = false;
      saveConfig();
      Serial.println("[CONFIG] SD auto-mount: OFF");
    }

    // SD save period
    else if (param == "sdsave 3") {
      config.sdSavePeriod = 3000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 3 seconds");
    } else if (param == "sdsave 60") {
      config.sdSavePeriod = 60000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 60 seconds");
    } else if (param == "sdsave 600") {
      config.sdSavePeriod = 600000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 600 seconds (10 min)");
    } else if (param == "sdsave 1200") {
      config.sdSavePeriod = 1200000;
      saveConfig();
      Serial.println("[CONFIG] SD save period: 1200 seconds (20 min)");
    }

    // HTTP send period
    else if (param == "httpsend 3") {
      config.httpSendPeriod = 3000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 3 seconds");
    } else if (param == "httpsend 60") {
      config.httpSendPeriod = 60000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 60 seconds");
    } else if (param == "httpsend 600") {
      config.httpSendPeriod = 600000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 600 seconds (10 min)");
    } else if (param == "httpsend 1200") {
      config.httpSendPeriod = 1200000;
      saveConfig();
      Serial.println("[CONFIG] HTTP send period: 1200 seconds (20 min)");
    }

    // HTTP timeout
    else if (param.startsWith("httptimeout ")) {
      int timeout = param.substring(12).toInt();
      if (timeout >= 5 && timeout <= 30) {
        config.httpTimeout = timeout;
        saveConfig();
        Serial.printf("[CONFIG] HTTP timeout: %d seconds\n", timeout);
      } else {
        Serial.println("[CONFIG] ✗ Timeout must be 5-30 seconds");
      }
    }

    // OLED auto-off
    else if (param == "oledoff on") {
      config.oledAutoOff = true;
      saveConfig();
      Serial.println("[CONFIG] OLED auto-off: ON");
    } else if (param == "oledoff off") {
      config.oledAutoOff = false;
      saveConfig();
      // Enable power
      // u8g2.setPowerSave(0); // Extern not available here directly, will
      // handle loop
      Serial.println("[CONFIG] OLED auto-off: OFF (always on)");
    }

    // OLED timeout
    else if (param == "oledtime 60") {
      config.oledTimeout = 60000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 60 seconds");
    } else if (param == "oledtime 120") {
      config.oledTimeout = 120000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 120 seconds (2 min)");
    } else if (param == "oledtime 180") {
      config.oledTimeout = 180000;
      saveConfig();
      Serial.println("[CONFIG] OLED timeout: 180 seconds (3 min)");
    }

    // LED enable/disable
    else if (param == "led on") {
      config.ledEnabled = true;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] NeoPixel: ON");
    } else if (param == "led off") {
      config.ledEnabled = false;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] NeoPixel: OFF");
    }

    // LED brightness
    else if (param == "ledbright 10") {
      config.ledBrightness = 10;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 10%");
    } else if (param == "ledbright 25") {
      config.ledBrightness = 25;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 25%");
    } else if (param == "ledbright 50") {
      config.ledBrightness = 50;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 50%");
    } else if (param == "ledbright 100") {
      config.ledBrightness = 100;
      saveConfig();
      applyLEDConfig();
      Serial.println("[CONFIG] LED brightness: 100%");
    }

    // Autostart
    else if (param == "autostart on") {
      config.autostart = true;
      saveConfig();
      Serial.println("[CONFIG] Autostart: ON (will start on next boot)");
    } else if (param == "autostart off") {
      config.autostart = false;
      saveConfig();
      Serial.println("[CONFIG] Autostart: OFF");
    }

    else if (param == "gnss on") {
      config.gnssEnabled = true;
      saveConfig();
      Serial.println("[CONFIG] GNSS: ON (requires reboot)");
    } else if (param == "gnss off") {
      config.gnssEnabled = false;
      saveConfig();
      Serial.println("[CONFIG] GNSS: OFF (requires reboot)");
    }

    // Autostart wait GPS
    else if (param == "autowaitgps on") {
      config.autostartWaitGps = true;
      saveConfig();
      Serial.println("[CONFIG] Autostart wait GPS: ON (will wait for fix)");
    } else if (param == "autowaitgps off") {
      config.autostartWaitGps = false;
      saveConfig();
      Serial.println("[CONFIG] Autostart wait GPS: OFF");
    }

    // Autostart GPS timeout
    else if (param.startsWith("autogpsto ")) {
      int val = param.substring(10).toInt();
      if (val >= 60 && val <= 900) {
        config.autostartGpsTimeout = val;
        saveConfig();
        Serial.printf("[CONFIG] Autostart GPS timeout: %u seconds (%u min)\n",
                      val, val / 60);
      } else {
        Serial.println(
            "[CONFIG] ✗ Invalid timeout (must be 60-900 seconds / 1-15 min)");
      }
    }

    // Autodebug
    else if (param == "autodebug on") {
      config.autoDebug = true;
      saveConfig();
      Serial.println("[CONFIG] Auto Debug: ON (will start on next boot)");
    } else if (param == "autodebug off") {
      config.autoDebug = false;
      saveConfig();
      Serial.println("[CONFIG] Auto Debug: OFF");
    }

    // Rotate display
    else if (param == "rotatedisplay on") {
      config.rotateDisplay = true;
      saveConfig();
      if (oledOK) u8g2.setDisplayRotation(U8G2_R0);
      Serial.println("[CONFIG] Rotate Display: ON");
    } else if (param == "rotatedisplay off") {
      config.rotateDisplay = false;
      saveConfig();
      if (oledOK) u8g2.setDisplayRotation(U8G2_R2);
      Serial.println("[CONFIG] Rotate Display: OFF");
    }

    // Scheduled reboot
    else if (param.startsWith("reboot ")) {
      String valStr = param.substring(7);
      valStr.trim();
      bool numeric = valStr.length() > 0;
      for (size_t i = 0; i < valStr.length(); ++i) {
        if (!isDigit(valStr[i])) {
          numeric = false;
          break;
        }
      }

      int val = numeric ? valStr.toInt() : -1;
      if (val >= 0 && val <= 720) {
        config.scheduledRebootHours = (uint16_t)val;
        saveConfig();
        if (val == 0) {
          Serial.println("[CONFIG] Scheduled reboot: OFF");
        } else {
          Serial.printf("[CONFIG] Scheduled reboot: every %d hours\n", val);
        }
      } else {
        Serial.println("[CONFIG] Invalid reboot value (use 0-720 hours)");
      }
    }

    // GNSS mode
    else if (param.startsWith("gnssmode ")) {
      int val = param.substring(9).toInt();
      if (val == 1 || val == 3 || val == 5 || val == 7 || val == 15) {
        config.gnssMode = val;
        saveConfig();
        Serial.print("[CONFIG] GNSS mode: ");
        Serial.print(val);
        Serial.println(" (Requires Reboot)");
      } else {
        Serial.println("[CONFIG] ✗ Invalid mode (valid: 1, 3, 5, 7, 15)");
      }
    }

    else {
      Serial.println("[CONFIG] ✗ Unknown parameter. Type 'help' for list.");
    }
  }

  else if (cmd == "configreset") {
    Serial.println("[CONFIG] Resetting to defaults...");
    configSetDefaults();
    saveConfig();
    applyLEDConfig();
    Serial.println("[CONFIG] ✓ Reset complete. Reboot to apply all changes.");
  }

  else if (cmd == "configsave") {
    saveConfig();
  }

  // -------------------- UNKNOWN COMMAND --------------------
  else {
    Serial.println("[CMD] Unknown command. Type 'help' for list.");
  }

  Serial.println();
}


#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\ui.ino"
// -------------------- UI & Display Logic --------------------
// Integrates functionality from HIRI_PR0_MENU with GPSDebug backend data
#include <OneButton.h>
#include <U8g2lib.h>
#include <string.h>

// -------------------- External Variables (from FirmwarePro.ino)
// --------------------
#include "config.h"//se trae en cada pesta;a que se necesita
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
// U8g2lib.h already included above

extern SPIClass spiSD;
extern Preferences prefs;
extern const int SD_CS;
extern const int SD_SCLK;
extern const int SD_MISO;
extern const int SD_MOSI;

// Info Screen Helpers & Others
extern String networkOperator;
extern String networkTech;
extern String registrationStatus;
extern String signalQuality;
extern bool syncRtcFromModem();
extern String getNetworkTime();
extern uint32_t getCsvFileSize();
extern String generateCSVFileName();
extern void writeCSVHeader();
extern void writeErrorLogHeader();

extern RTC_DS3231 rtc;
extern TinyGsm modem;
extern bool rtcOK;
extern bool SDOK;
extern bool wifiModeActive;
extern bool hasRed;

extern bool loggingEnabled;
extern bool streaming;
extern bool haveFix;
extern String gpsStatus;
extern float batV;
extern String gpsLat;
extern String gpsLon;
extern String gpsSpeedKmh;
extern uint32_t lastHttpActivityMs;
extern uint32_t lastSdActivityMs;
extern bool lastHttpOk;
extern bool lastSdOk;
extern bool hasHttpAttempted;
extern uint16_t PM25;
extern float pmsTempC;
extern float pmsHum;
extern String satellitesStr;
extern struct AtSession at;
extern SystemConfig config;
extern volatile enum DisplayState displayState;
extern volatile uint32_t displayStateStartTime;
extern uint32_t lastOledActivity;
extern String csvFileName;
extern uint8_t debugScreenIndex;
extern uint32_t lastDebugRotationMs;
extern const uint32_t DEBUG_ROTATION_INTERVAL_MS;
extern bool SHT4xOK, GasOK, ENS160OK;
extern float tempsht4x, humsht4x, gasPpm;
extern String hdopStr, gpsAlt;
extern uint32_t sendCounter, sdSaveCounter;
extern int SDS198PM100;
extern uint16_t PM10, PM1;
extern bool sendCurrentMeasurement();
extern bool saveCSVData();
extern uint16_t ens160Tvoc, ens160Eco2;
extern uint8_t ens160Aqi, ens160StatusRaw;
extern bool ens160DataValid;
extern bool httpBackoffActive;

// -------------------- Helper Logic --------------------

extern String apIpStr;
extern String AP_SSID_STR;
extern const char *AP_PASSWORD;

// --- Helper Logic ---

// Pantalla dedicada de control WiFi (estilo RTC)
void drawWifiModeScreen() {
  if (!oledOK) return;
  u8g2.clearBuffer();
  drawHeader();
  u8g2.setFont(u8g2_font_6x10_tf);

  u8g2.drawStr(0, 20, "WIFI SD:");
  u8g2.drawStr(50, 20, wifiModeActive ? "ACTIVO" : "INACTIVO");

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 31, ("SSID: " + AP_SSID_STR).c_str());
  u8g2.drawStr(0, 40, ("PASS: " + String(AP_PASSWORD)).c_str());

  String ip = wifiModeActive ? apIpStr : String("0.0.0.0");
  u8g2.drawStr(0, 49, ("IP: " + ip).c_str());

  u8g2.drawFrame(0, 50, 128, 14);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 60, wifiModeActive ? "B1:---" : "B1:EXIT");
  u8g2.drawStr(52, 60, wifiModeActive ? "B2:OFF" : "B2:ON");

  u8g2.sendBuffer();
}

// Centralized Toggle Logic for Starting/Stopping Sampling
void toggleSamplingAction() {
  if (streaming) {
      // STOP
      streaming = false;
      loggingEnabled = false;
      prefs.begin("system", false);
      prefs.putBool("streaming", false);
      prefs.end();
      showMessage("DETENIDO");
      Serial.println("[UI] Sampling STOPPED");
  } else {
      // START
      Serial.println("[UI] Starting sampling...");
      streaming = true;
      if (!SDOK) {
        spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
        SDOK = SD.begin(SD_CS, spiSD);
      }
      if (SDOK) {
        csvFileName = generateCSVFileName();
        writeCSVHeader();
        prefs.begin("system", false);
        prefs.putString("csvFile", csvFileName);
        prefs.end();
        Serial.println("[UI] SD OK, logging enabled");
      }
      loggingEnabled = SDOK;
      prefs.begin("system", false);
      prefs.putBool("streaming", true);
      prefs.end();
      
      // Force immediate execution in loop
      extern uint32_t lastHttpSend;
      extern uint32_t lastSdSave;
      lastHttpSend = 0; 
      lastSdSave = 0;
      
      showMessage("INICIADO");
      Serial.println("[UI] Sampling STARTED");
  }
}


#define SD_SAVE_DISPLAY_MS 2000

// --- Menu Structure ---
// Struct Menu
struct Menu {
  const char **items;
  const uint16_t *icons;
  uint8_t count;
};

// -------------------- Bitmap Icons --------------------
// Satellite icon 8x8, 1 bit/pixel, LSB first
static const unsigned char PROGMEM satelit_bitmap[8] = {0x06, 0x6E, 0x74, 0x38,
                                                        0x58, 0xE5, 0xC1, 0x07};

// Global for non-blocking UI messages
char uiMessage[21] = "";
uint32_t uiMessageDurationMs = DISP_MSG_DURATION_MS;

// Helper to show message non-blocking
void showMessage(const char *msg) {
  strncpy(uiMessage, msg, sizeof(uiMessage) - 1);
  uiMessage[sizeof(uiMessage) - 1] = '\0';
  uiMessageDurationMs = DISP_MSG_DURATION_MS;
  displayState = DISP_MESSAGE;
  displayStateStartTime = millis();
}

void showMessageFor(const char *msg, uint32_t durationMs) {
  strncpy(uiMessage, msg, sizeof(uiMessage) - 1);
  uiMessage[sizeof(uiMessage) - 1] = '\0';
  uiMessageDurationMs = durationMs;
  displayState = DISP_MESSAGE;
  displayStateStartTime = millis();
}

// Menú Principal
const char *topItems[] = {"PM2.5", "TEMPERATURA", "HUMEDAD", "EMPEZAR MUESTREO",
                          "OPCIONES"};
const uint16_t topIcons[] = {
    0,      // null
    0,      // null
    0,      // null
    0x01A5, // muestreo
    0x0192  // opciones
};

// Submenu: Opciones
const char *SubItems[] = {"MENSAJES", "CONFIGURACION", "INFORMACION", "VOLVER"};
const uint16_t SubIcons[] = {
    0x00EC, // mensajes
    0x015b, // configuración
    0x0185, // información
    0x01A9  // volver
};

// Menú de “Mensajes”
const char *msgItems[] = {"CAMION", "HUMO", "CONSTRUCCION", "OTROS", "VOLVER"};
const uint16_t msgIcons[] = {
    0x2A1, // 🚚 Camión
    0x26C, // 💨 Humo
    0x09E, // 🏗️ Construcción
    0x00A0, // Otros (ícono genérico)
    0x01A9 // ←   Volver
};

// Menú de “Configuración”
// Menú de “Configuración”
const char *cfgItems[] = {"RTC", "REINICIAR", "VOLVER"};
const uint16_t cfgIcons[] = {
    0x01CB, // rtc/función
    0x00D5, // reiniciar
    0x01A9  // volver
};

// Menú de “Información”
// Menú de “Información”
const char *infoItems[] = {"VERSION", "REDES", "GPS", "GUARDADO", "WIFI SD (ON/OFF)", "MODO DEBUG", "VOLVER"};
const uint16_t infoIcons[] = {
    0x0085, // version
    0x01CC, // redes
    0x01A5, // GPS
    0x0176, // guardado
    0x0093, // wifi sd
    0x0185, // modo debug (full)
    0x01A9  // volver
};

Menu menus[] = {
    {topItems, topIcons, sizeof(topItems) / sizeof(topItems[0])}, // 0: Main
    {SubItems, SubIcons, sizeof(SubItems) / sizeof(SubItems[0])}, // 1: Opciones
    {msgItems, msgIcons, sizeof(msgItems) / sizeof(msgItems[0])}, // 2: Mensajes
    {cfgItems, cfgIcons,
     sizeof(cfgItems) / sizeof(cfgItems[0])}, // 3: Configuración
    {infoItems, infoIcons, sizeof(infoItems) / sizeof(infoItems[0])} // 4: Info
};

uint8_t menuDepth = 0; // 0 = principal, 1+ = submenus
uint8_t menuIndex = 0; // Índice seleccionado
bool uiFullMode = false;

// Guard de acciones UI para evitar dobles disparos por rebote/eventos
// solapados.
static uint32_t uiLastActionMs = 0;
const uint32_t UI_ACTION_GUARD_MS = 70;

static bool uiCanHandleAction() {
  uint32_t now = millis();
  if (now - uiLastActionMs < UI_ACTION_GUARD_MS)
    return false;
  uiLastActionMs = now;
  return true;
}

// --- Button Instances (defined in main but used here) ---
// Declared extern in main helpers if needed, but we can access valid objects if
// they are global. We will define specific handler functions here that main
// will attach.

// --- UI Helper Functions ---
// Devuelve hora formateada HH:MM:SS desde RTC para cabecera OLED.
// Si RTC falla, entrega placeholder seguro.
String getClockTime() {
  if (!rtcOK)
    return "??:??:??";
  DateTime now = rtc.now();
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.hour(), now.minute(),
           now.second());
  return String(buf);
}

// Convierte voltaje de batería a porcentaje aproximado de carga.
// Usa umbrales operativos del sistema para evitar valores irreales.
int calcBatteryPercent(float v) {
  if (v >= 4.1)
    return 100; // 4.2V = 100%
  if (v <= 3.3)
    return 0;                          // 3.4V = 0% (límite operacional ESP32)
  return (int)((v - 3.4) / 0.8 * 100); // Rango: 3.4V-4.2V = 0.8V
}

// Dibuja icono de batería dinámico en OLED según voltaje estimado.
// Incluye fallback visual para estado crítico/sin lectura válida.
void drawBatteryDynamic(int xPos, int yPos, float v) {
  // Validar voltaje para evitar valores inválidos
  if (isnan(v) || v < 0 || v > 5.0)
    v = 3.4;

  int pct = calcBatteryPercent(v);
  // Limitar porcentaje entre 0-100
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;

  float frac = pct / 100.0;
  const uint8_t w = 9, h = 6, tip = 2;
  // Posición ajustable
  uint8_t x = xPos;
  uint8_t y = yPos;
  // Contorno y terminal
  u8g2.drawFrame(x, y, w, h);
  u8g2.drawBox(x + w, y + 2, tip, h - 4);

  // Nivel interno o icono de carga crítica
  if (pct == 0) {
    // si es 0 ponemos la C
    u8g2.setFont(u8g2_font_5x7_tf);
    char s = 'C';
    u8g2.setCursor(x - 6, y + h);
    u8g2.print(s);

    u8g2.drawLine(x + 4, y + 1, x + 2, y + 3);
    u8g2.drawLine(x + 2, y + 3, x + 5, y + 3);
    u8g2.drawLine(x + 5, y + 3, x + 3, y + 5);
  } else {
    // Calcular ancho del relleno y limitar al tamaño del marco
    uint8_t fillWidth = (uint8_t)((w - 2) * frac);
    if (fillWidth > (w - 2))
      fillWidth = (w - 2);
    if (fillWidth > 0) {
      u8g2.drawBox(x + 1, y + 1, fillWidth, h - 2);
    }
  }
}

// Dibuja un indicador mínimo de estado para TX/SD sin ocupar mucho header.
// enabled=feature ON, active=actividad reciente, ok=último resultado.
void drawActivityDot(int x, bool enabled, bool active, bool ok) {
  int y = 6; // posicion vertical mas grande el numero mas abajo
  if (!enabled) {
    u8g2.drawCircle(x, y, 2);
    return;
  }

  if (active)
    u8g2.drawDisc(x, y, 2);
  else
    u8g2.drawCircle(x, y, 2);

  if (!ok) {
    u8g2.drawPixel(x + 3, y - 3);
  }
}

void drawHeader() {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 9, getClockTime().c_str());

  // Indicadores críticos mínimos (TX/SD) para no romper layout del header.
  uint32_t now = millis();
  bool txActive = (now - lastHttpActivityMs) < 1200;
  bool sdActive = (now - lastSdActivityMs) < 1200;
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(41, 9, "S");
  u8g2.drawStr(51, 9, "G");
  drawActivityDot(47, streaming, txActive, lastHttpOk);
  drawActivityDot(57, loggingEnabled, sdActive, lastSdOk);

  // Satellite icon + satélites (movido +10 px para evitar solape)
  if (!config.gnssEnabled) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(68, 9, "N");
  } else if (haveFix && gpsStatus == "Fix") {
    u8g2.drawXBMP(63, 1, 8, 8, satelit_bitmap);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(73, 9);
    String sats = satellitesStr;
    if (sats.length() > 2)
      sats = "99";
    u8g2.print(sats);
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(68, 9, 0x0118);
  }

  // Signal / HTTP state (movido +10 px para evitar solape)
  bool networkTxReady = (csq != 99) && hasRed &&
                        (registrationStatus == "Registered") &&
                        !httpBackoffActive;
  bool networkWarn = !networkTxReady || (hasHttpAttempted && !lastHttpOk);
  if (networkWarn) {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(84, 9, 0x0118);
    if (httpBackoffActive) {
      u8g2.setFont(u8g2_font_4x6_tf);
      u8g2.drawStr(92, 9, "P");
    }
  } else {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(84, 9, 0x00FD);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(92, 9);
    String csqStr = String(csq);
    if (csqStr.length() > 2)
      csqStr = csqStr.substring(0, 2);
    u8g2.print(csqStr);
  }

  // Batería al extremo derecho.
  drawBatteryDynamic(110, 3, batV);
}

// Dibuja indicadores de paginación del menú en el footer OLED.
// Marca visualmente el item seleccionado.
void drawFooterCircles(uint8_t cnt, uint8_t sel) {
  const uint8_t dia = 4, sp = 8;
  uint8_t totalW = cnt * dia + (cnt - 1) * sp;
  int16_t sx = (128 - totalW) / 2;
  for (uint8_t i = 0; i < cnt; i++) {
    uint8_t x = sx + i * (dia + sp);
    if (i == sel)
      u8g2.drawDisc(x + dia / 2, 59, dia / 2);
    else
      u8g2.drawCircle(x + dia / 2, 59, dia / 2);
  }
}

// Presenta valor grande de sensor según pantalla activa (PM/Temp/Hum).
// Centra texto y etiqueta para lectura rápida en terreno.
void drawSensorValue(uint8_t idx) {
  char buf[24];
  String baseF = "";
  if (idx == 0) { // PM2.5
    snprintf(buf, sizeof(buf), "%u", PM25);
    baseF = "PM 2.5 (ug/m3)";
  } else if (idx == 1) { // Temp
    if (isnan(pmsTempC))
      snprintf(buf, sizeof(buf), "--.-");
    else
      dtostrf(pmsTempC, 0, 1, buf);
    baseF = "Temp PMS (C)";
  } else if (idx == 2) { // Hum
    if (isnan(pmsHum))
      snprintf(buf, sizeof(buf), "--.-");
    else
      dtostrf(pmsHum, 0, 1, buf);
    baseF = "Hum PMS (%)";
  }

  u8g2.setFont(u8g2_font_logisoso24_tn);
  int w = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - w) / 2, 43, buf);

  u8g2.setFont(u8g2_font_5x7_tf);
  int wLbl = u8g2.getStrWidth(baseF.c_str());
  u8g2.drawStr((128 - wLbl) / 2, 53, baseF.c_str());
}

// Dibuja item de menú con icono y texto centrados.
// Reutiliza estructuras de menú para mantener UI modular.
void drawMenuItemWithIcon(uint8_t depth, uint8_t idx) {
  const char *txt = menus[depth].items[idx];
  const uint16_t *ic = menus[depth].icons;

  // 1) Dibuja icono centrado y encima del texto
  if (ic && ic[idx] != 0) {
    // Submenús usan streamline_all_t (16x16 aprox)
    u8g2.setFont(u8g2_font_streamline_all_t);
    uint8_t iconW = u8g2.getMaxCharWidth();
    u8g2.drawGlyph((128 - iconW) / 2, 38, ic[idx]);
  }

  // 2) Dibuja texto centrado horizontalmente
  u8g2.setFont(u8g2_font_6x12_tf);
  int tw = u8g2.getStrWidth(txt);
  u8g2.drawStr((128 - tw) / 2, 53, txt);
}

// Vista DEBUG con rotación de pantallas.
void drawFullModeView() {
  u8g2.setFont(u8g2_font_5x7_tf);
  uint8_t debugScreenCount = config.gnssEnabled ? 4 : 3;
  uint8_t screenIdx = config.gnssEnabled ? debugScreenIndex : (debugScreenIndex + 1);
  
  // Cabecera de la pantalla actual
  u8g2.setCursor(110, 16);
  u8g2.print(String(debugScreenIndex + 1) + "/" + String(debugScreenCount));

  if (screenIdx == 0) { // --- SCREEN 0: GPS & TIME ---
    u8g2.drawStr(0, 16, "DEBUG: GPS & TIME");
    u8g2.setCursor(0, 26);
    u8g2.print("Lat:" + gpsLat);
    u8g2.setCursor(0, 34);
    u8g2.print("Lon:" + gpsLon);
    u8g2.setCursor(0, 42);
    u8g2.print("Alt:" + gpsAlt + "m Spd:" + gpsSpeedKmh + "km/h");
    u8g2.setCursor(0, 50);
    u8g2.print("Sats:" + satellitesStr + " HDOP:" + hdopStr);
    
    DateTime now = rtcOK ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", now.day(), now.month(), now.year(), now.hour(), now.minute());
    u8g2.drawStr(0, 58, buf);

  } else if (screenIdx == 1) { // --- SCREEN 1: PM (AIR QUALITY) ---
    u8g2.drawStr(0, 16, "DEBUG: AIR QUALITY (PM)");
    u8g2.setCursor(0, 26);
    u8g2.print("PM 1.0:  " + String(PM1) + " ug/m3");
    u8g2.setCursor(0, 34);
    u8g2.print("PM 2.5:  " + String(PM25) + " ug/m3");
    u8g2.setCursor(0, 42);
    u8g2.print("PM 10:   " + String(PM10) + " ug/m3");
    if (SDS198OK) {
      u8g2.setCursor(0, 50);
      u8g2.print("PM 100:  " + String(SDS198PM100) + " ug/m3 (SDS)");
    }
    u8g2.setCursor(0, 58);
    u8g2.print("PMS T/H: " + String(pmsTempC, 1) + "C / " + String(pmsHum, 1) + "%");

  } else if (screenIdx == 2) { // --- SCREEN 2: ENV & GAS ---
    u8g2.drawStr(0, 16, "DEBUG: ENV & GAS");
    int y = 26;
    u8g2.setCursor(0, y);
    if (!isnan(tempsht4x) && !isnan(humsht4x)) {
      u8g2.print("SHT40: " + String(tempsht4x, 1) + "C / " + String(humsht4x, 1) + "%");
    } else {
      u8g2.print("SHT40: --.-C / --.-%");
    }
    y += 8;
    if (ENS160OK && ens160DataValid) {
      u8g2.setCursor(0, y);
      u8g2.print("AQI:" + String(ens160Aqi) + " TVOC:" + String(ens160Tvoc) + "ppb");
      y += 8;
      u8g2.setCursor(0, y);
      u8g2.print("eCO2: " + String(ens160Eco2) + " ppm");
      y += 8;
    } else if (ENS160OK) {
      u8g2.setCursor(0, y);
      u8g2.print("ENS160 INVALID ST:" + String(ens160StatusRaw));
      y += 8;
    } else {
      u8g2.setCursor(0, y);
      u8g2.print("ENS160: OFFLINE");
      y += 8;
    }
    u8g2.setCursor(0, y);
    if (!isnan(gasPpm)) {
      u8g2.print("GAS: " + String(gasPpm, 2) + " PPM");
    } else {
      u8g2.print("GAS: --.-- PPM");
    }

  } else if (screenIdx == 3) { // --- SCREEN 3: SYSTEM ---
    u8g2.drawStr(0, 16, "DEBUG: SYSTEM STATUS");
    u8g2.setCursor(0, 26);
    u8g2.print("ID: " + String(DEVICE_ID_STR) + " VER: " + VERSION);
    u8g2.setCursor(0, 34);
    u8g2.print("BAT: " + String(batV, 2) + "V (" + String(calcBatteryPercent(batV)) + "%)");
    u8g2.setCursor(0, 42);
    u8g2.print("SIG: " + signalQuality + " CSQ (" + networkTech + ")");
    u8g2.setCursor(0, 50);
    u8g2.print("TX: " + String(sendCounter) + " SD: " + String(sdSaveCounter));
    u8g2.setCursor(0, 58);
    u8g2.print(streaming ? (loggingEnabled ? "MODE: STREAM + LOG" : "MODE: STREAM ONLY") : "MODE: STANDBY");
  }
}

// Render principal de OLED con estado normal y estados transitorios.
// Integra cabecera, cuerpo de menú y footer en cada refresco.
void renderDisplay() {
  if (!oledOK) return;
  u8g2.clearBuffer();
  drawHeader();

  // Special States
  if (displayState == DISP_MESSAGE) {
    if (millis() - displayStateStartTime < uiMessageDurationMs) {
      u8g2.setFont(u8g2_font_6x12_tf);
      // Center message
      int w = u8g2.getStrWidth(uiMessage);
      u8g2.drawStr((128 - w) / 2, 35, uiMessage);
      drawFooterCircles(menus[menuDepth].count, menuIndex); // Keep context
      u8g2.sendBuffer();
      return;
    } else {
      displayState = DISP_NORMAL;
    }
  }

  if (displayState == DISP_PROMPT) {
    u8g2.drawFrame(0, 12, 128, 52);
    u8g2.setFont(u8g2_font_6x12_tf);
    const char* l1 = "CONFIRMAR ACCION?";
    const char* l2 = streaming ? "DETENER MUESTREO" : "INICIAR MUESTREO";
    u8g2.drawStr((128 - u8g2.getStrWidth(l1)) / 2, 28, l1);
    u8g2.drawStr((128 - u8g2.getStrWidth(l2)) / 2, 42, l2);
    
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(5, 58, "BTN1: NO");
    u8g2.drawStr(80, 58, "BTN2: SI");
    
    u8g2.sendBuffer();
    return;
  }
  
  if (displayState == DISP_NETWORK) {
    if (millis() - displayStateStartTime > 5000) {
      displayState = DISP_NORMAL;
      renderDisplay();
      return;
    }
    drawNetworkInfo();
    return;
  }
  if (displayState == DISP_RTC) {
    drawRtcInfo();
    return;
  }
  if (displayState == DISP_STORAGE) {
    if (millis() - displayStateStartTime > 5000) {
      displayState = DISP_NORMAL;
      renderDisplay();
      return;
    }
    drawStorageInfo();
    return;
  }
  if (displayState == DISP_GPS) {
    if (millis() - displayStateStartTime > 10000) {
      displayState = DISP_NORMAL;
      renderDisplay();
      return;
    }
    drawGpsInfo();
    return;
  }

  if (displayState == DISP_WIFI) {
    drawWifiModeScreen();
    return;
  }

  // FULL mode (sin menú) - UPDATED TO DEBUG MODE ROTATION
  if (uiFullMode) {
    uint8_t debugScreenCount = config.gnssEnabled ? 4 : 3;
    if (millis() - lastDebugRotationMs >= DEBUG_ROTATION_INTERVAL_MS) {
      lastDebugRotationMs = millis();
      debugScreenIndex = (debugScreenIndex + 1) % debugScreenCount;
    }
    drawFullModeView();
    u8g2.sendBuffer();
    return;
  }

  // Normal Menu Rendering
  if (menuDepth == 0) {
    // Menu Principal
    // Items 0-2 son sensores (PM2.5, Temp, Hum)
    if (menuIndex < 3) {
      drawSensorValue(menuIndex);
    } else {
      // Items 3+ (Infos, Opciones)
      const char *txt = (menuIndex == 3) ? (streaming ? "DETENER MUESTREO" : "EMPEZAR MUESTREO") : menus[0].items[menuIndex];
      u8g2.setFont(u8g2_font_6x12_tf);
      int tw = u8g2.getStrWidth(txt);
      u8g2.drawStr((128 - tw) / 2, 53, txt);

      const uint16_t *ic = menus[0].icons;
      if (ic && ic[menuIndex] != 0) {
        // Use consistent font for icons (Streamline matches the codes used)
        u8g2.setFont(u8g2_font_streamline_all_t);
        uint8_t iconW = u8g2.getMaxCharWidth();
        u8g2.drawGlyph((128 - iconW) / 2, 35, ic[menuIndex]);
      }
    }
  } else {
    // Submenus
    drawMenuItemWithIcon(menuDepth, menuIndex);
  }

  drawFooterCircles(menus[menuDepth].count, menuIndex);
  u8g2.sendBuffer();
}

// --- Specific Info Screens ---

void drawNetworkInfo() {
  u8g2.clearBuffer();
  drawHeader();
  u8g2.setFont(u8g2_font_6x10_tf);
  
  u8g2.drawStr(0, 20, "RED:");
  u8g2.drawStr(30, 20, networkOperator.c_str());
  
  u8g2.drawStr(0, 32, "TEC:");
  u8g2.drawStr(30, 32, networkTech.c_str());
  
  u8g2.drawStr(0, 44, "SIG:");
  String sigStr = signalQuality + " CSQ";
  u8g2.drawStr(30, 44, sigStr.c_str());
  
  u8g2.drawStr(0, 56, "EST:");
  u8g2.drawStr(30, 56, registrationStatus.c_str());
  
  // Footer hint
  u8g2.setFont(u8g2_font_5x7_tf);
  // u8g2.drawStr(80, 62, "BTN1:SALIR"); // Removed for auto-timeout
  
  u8g2.sendBuffer();
}

void drawRtcInfo() {
  u8g2.clearBuffer();
  drawHeader();
  u8g2.setFont(u8g2_font_6x10_tf);
  
  // Line 1: RTC
  String rtcTime = "??:??:??";
  if (rtcOK) {
      DateTime now = rtc.now();
      char buf[20];
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d %02d/%02d", now.hour(), now.minute(), now.second(), now.day(), now.month());
      rtcTime = String(buf);
  }
  u8g2.drawStr(0, 25, ("RTC: " + rtcTime).c_str());
  
  // Line 2: Net
  String netTime = getNetworkTime();
  u8g2.drawStr(0, 40, ("NET: " + netTime).c_str());
  
  // Line 3: Action
  u8g2.drawFrame(0, 48, 128, 15);
  u8g2.drawStr(15, 59, "BTN2: SYN");
  u8g2.drawStr(80, 59, "B1:EXIT");
  
  u8g2.sendBuffer();
}

void drawStorageInfo() {
  u8g2.clearBuffer();
  drawHeader();
  u8g2.setFont(u8g2_font_6x10_tf);
  
  u8g2.drawStr(0, 22, "ARCHIVO ACTUAL:");
  u8g2.drawStr(0, 34, csvFileName.length() > 0 ? csvFileName.c_str() : "(Ninguno)");
  
  u8g2.drawStr(0, 48, "TAMANO:");
  uint32_t sz = getCsvFileSize();
  String sizeStr;
  if (sz < 1024) sizeStr = String(sz) + " B";
  else if (sz < 1024*1024) sizeStr = String(sz/1024) + " KB";
  else sizeStr = String(sz/(1024.0*1024.0), 2) + " MB";
  
  u8g2.drawStr(50, 48, sizeStr.c_str());
  
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 60, streaming ? "EN GRABACION..." : "DETENIDO");
  
  u8g2.sendBuffer();
}

void drawGpsInfo() {
  u8g2.clearBuffer();
  drawHeader();
  u8g2.setFont(u8g2_font_6x10_tf);
  
  u8g2.drawStr(0, 20, "ESTADO GPS:");
  u8g2.drawStr(70, 20, (haveFix && gpsStatus == "Fix") ? "FIX OK" : "NO FIX");
  
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 32, ("Lat: " + gpsLat).c_str());
  u8g2.drawStr(0, 42, ("Lon: " + gpsLon).c_str());
  u8g2.drawStr(0, 52, ("Vel: " + gpsSpeedKmh + " km/h").c_str());
  
  u8g2.sendBuffer();
}


// --- Action Handlers ---

// Muestra aviso visual y reinicia el ESP32 de forma controlada.
// Se ejecuta desde menú de configuración.
void handleRestart() {
  showMessage("REINICIANDO...");
  // Nota: ESP.restart() ocurrirá después, aquí solo iniciamos el mensaje
  // En un sistema real no bloqueante, deberíamos setear un flag para reiniciar
  // luego del mensaje Pero para simplificar, usaremos un pequeño delay justo
  // antes del restart real si fuera crítico, aqui solo mostramos y esperamos un
  // poco. Dado que restart mata todo, un delay aqui es aceptable
  // excepcionalmente o mejor: no usamos delay, pero el usuario no verá mucho si
  // reinicia de inmediato. Para hacerlo VERDADERAMENTE no bloqueante,
  // necesitariamos un "pendingRestart" flag. Por ahora, aceptamos que REINICIO
  // es una acción terminal.
  delay(1000);
  ESP.restart();
}

// Alterna modo WiFi AP para gestión de archivos en SD.
// Inicia o detiene servidor web según estado actual.
void handleConfigWifi() {
  // Toggle WiFi AP
  if (!wifiModeActive) {
    startWifiApServer(); // This will take over display
  } else {
    stopWifiApServer();
  }
}

// --- Interaction Logic ---

// BTN1 Click: Next Option
// Evento BTN1: avanza selección en el menú activo.
// Reactiva OLED si estaba en ahorro de energía.
void ui_btn1_click() {
  Serial.println("[UI] BTN1 Click");

  if (!uiCanHandleAction())
    return;

  if (displayState == DISP_PROMPT) {
    displayState = DISP_NORMAL;
    showMessage("CANCELADO");
    renderDisplay();
    return;
  }

  if (displayState == DISP_RTC) {
    displayState = DISP_NORMAL;
    renderDisplay();
    return;
  }

  if (displayState == DISP_WIFI) {
    // Regla solicitada:
    // - Si WiFi está ACTIVO: BTN1 no hace nada (bloqueado)
    // - Si WiFi está INACTIVO: BTN1 permite salir
    if (!wifiModeActive) {
      displayState = DISP_NORMAL;
    }
    renderDisplay();
    return;
  }

  if (uiFullMode) {
    // En modo FULL, BTN1 click ahora INICIA/DETIENE el muestreo (Acción rápida) ahora vamos a hacer una prueba de transmision de datos 
    // Reutilizamos la lógica de toggle que antes estaba en BTN2
    //testeo guardar datos tambien 
   // toggleSamplingAction(); en hiri pro para estaciones solo transmision
    bool txOk = sendCurrentMeasurement();
    hasHttpAttempted = true;
    lastHttpActivityMs = millis();
    lastHttpOk = txOk;

    bool previousLogging = loggingEnabled;
    if (SDOK) {
      loggingEnabled = true;
    }
    bool sdSaved = saveCSVData();
    loggingEnabled = previousLogging;
    lastSdActivityMs = millis();
    lastSdOk = sdSaved;

    char resultMsg[21];
    snprintf(resultMsg, sizeof(resultMsg), "TX %s SD %s",
             txOk ? "OK" : "FAIL", sdSaved ? "OK" : "FAIL");
    showMessageFor(resultMsg, 3000);
    Serial.println("[UI] BTN1 manual action: TX " + String(txOk ? "OK" : "FAIL") +
                   " SD " + String(sdSaved ? "OK" : "FAIL"));
    renderDisplay();
    return;
  }

  menuIndex = (menuIndex + 1) % menus[menuDepth].count;
  lastOledActivity = millis();
  if (oledOK && config.oledAutoOff)
    u8g2.setPowerSave(0);
  renderDisplay();
}

// BTN2 Click: Select / Enter
// Evento BTN2 corto: entra/selecciona opciones del menú.
// Controla navegación entre niveles y acciones no críticas.
void ui_btn2_click() {
  Serial.println("[UI] BTN2 Click");
  if (displayState == DISP_PROMPT) {
    // Perform Toggle using helper
    toggleSamplingAction();
    displayState = DISP_NORMAL;
    renderDisplay();
    return;
  }
  

  if (uiFullMode) {
    // En modo FULL, BTN2 click ahora SALE al menú principal
    uiFullMode = false;
    menuDepth = 0;
    menuIndex = 0; // Volver al inicio
    showMessage("MODO MENU");
    renderDisplay();
    return;
  }
  
  // Handle Info Screen Actions
  if (displayState == DISP_RTC) {
     showMessage("Sincronizando...");
     if (syncRtcFromModem()) {
       showMessage("SYNC OK");
     } else {
       showMessage("SYNC ERROR");
     }
     displayState = DISP_RTC; // Return to RTC screen
     renderDisplay();
     return;
  }
  if (displayState == DISP_WIFI) {
      bool wasActive = wifiModeActive;
      handleConfigWifi(); // toggle ON/OFF
      showMessage(wasActive ? "WIFI SD: OFF" : "WIFI SD: ON");
      displayState = DISP_WIFI; // permanecer en la pantalla WIFI
      renderDisplay();
      return;
  }

  if (displayState == DISP_NETWORK || displayState == DISP_STORAGE) {
      // Just refresh
      renderDisplay();
      return;
  }

  if (!uiCanHandleAction())
    return;
  lastOledActivity = millis();
  if (oledOK && config.oledAutoOff)
    u8g2.setPowerSave(0);

  if (menuDepth == 0) {
    // Main Menu
    if (menuIndex == 3) { // Empezar Muestreo (PROMPT)
      displayState = DISP_PROMPT;
    } else if (menuIndex == 4) { // Opciones
      menuDepth = 1;
      menuIndex = 0;
    }
  } else if (menuDepth == 1) {
    // Opciones Menu
    if (menuIndex == 0) { // Mensajes
      menuDepth = 2;
      menuIndex = 0;
    } else if (menuIndex == 1) { // Configuracion
      menuDepth = 3;
      menuIndex = 0;
    } else if (menuIndex == 2) { // Informacion
      menuDepth = 4;
      menuIndex = 0;
    } else if (menuIndex == 3) { // Volver
      menuDepth = 0;
      menuIndex = 0;
    }
  } else if (menuDepth == 2) {
    // Mensajes: Mostrar feedback NO BLOQUEANTE y mantenerse
    if (menuIndex == 0) {
      currentNote = "Camion";
      showMessage("Nota: Camion");
      Serial.println("[UI] Accion: Camion");
    } else if (menuIndex == 1) {
      currentNote = "Humo";
      showMessage("Nota: Humo");
      Serial.println("[UI] Accion: Humo");
    } else if (menuIndex == 2) {
      currentNote = "Construccion";
      showMessage("Nota: Construccion");
      Serial.println("[UI] Accion: Construccion");
    } else if (menuIndex == 3) { // Otros
      currentNote = "Otros";
      showMessage("Nota: Otros");
      Serial.println("[UI] Accion: Otros");
    } else if (menuIndex == 4) { // Volver
      menuDepth = 1;
      menuIndex = 0;
    }
  } else if (menuDepth == 3) {
    // Configuration Menu
    if (menuIndex == 0) { // RTC
      displayState = DISP_RTC;
    } else if (menuIndex == 1) { // Reiniciar
      handleRestart();
    } else if (menuIndex == 2) { // Volver
      menuDepth = 1;
      menuIndex = 0;
    }
  } else if (menuDepth == 4) {
    // Información
    if (menuIndex == 0) { // Version
      showMessage(VERSION.c_str());
    } else if (menuIndex == 1) { // Redes
      displayState = DISP_NETWORK;
      displayStateStartTime = millis();
    } else if (menuIndex == 2) { // GPS
      displayState = DISP_GPS;
      displayStateStartTime = millis();
    } else if (menuIndex == 3) { // Guardado
      displayState = DISP_STORAGE;
      displayStateStartTime = millis();
    } else if (menuIndex == 4) { // WIFI SD (ON/OFF)
      displayState = DISP_WIFI;
    } else if (menuIndex == 5) { // MODO DEBUG
      uiFullMode = true;
      displayState = DISP_NORMAL;
      lastDebugRotationMs = millis();
      debugScreenIndex = 0;
      showMessage("MODO DEBUG");
    } else if (menuIndex == 6) { // Volver
      menuDepth = 1;
      menuIndex = 0;
    }
  } else {
    // Generic Back for other menus if added later
    if (strcmp(menus[menuDepth].items[menuIndex], "Volver") == 0) {
      menuDepth--;
      menuIndex = 0;
    }
  }

  renderDisplay();
}

// BTN2 Hold: Eliminado / Vacío
// La lógica se ha simplificado para no usar Hold.
// BTN2 Hold: Eliminado / Vacío
// La lógica se ha simplificado para no usar Hold.
// (Function removed to clean up code)

// Placeholder de máquina de estados UI para futuras extensiones.
// Actualmente el estado se actualiza en handlers y renderDisplay().
void updateDisplayStateMachine() {
  // Nothing to update state-wise here, handled in renderDisplay and event
  // handlers
}

#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\wifi.ino"
///////////////////////////////////////////// WIFI
// ===== WiFi AP File Server (single definition) =====

// ---- HTML ----
const char *headerHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 SD Manager</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 800px; margin: auto; padding: 10px; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #f4f4f4; }
    button { margin: 2px; padding: 5px 10px; }
    input[type=file] { margin-top: 10px; }
    progress { width: 100%; margin-top: 5px; }
    #uploadStatus { display: inline-block; margin-left: 10px; }
    .toolbar { margin-top:10px; padding:10px; border:1px solid #ddd; background:#fafafa; }
    .error { color: red; font-weight: bold; }
    .loading { color: #666; font-style: italic; }
  </style>
  <script>
    function sanitizeId(s) {
      return (s || '').replace(/[^a-zA-Z0-9]/g, '_');
    }
    function downloadFile(fname) {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', '/download?file=' + encodeURIComponent(fname), true);
      xhr.responseType = 'blob';
      xhr.onprogress = function(event) {
        var percent = event.lengthComputable ? Math.floor((event.loaded / event.total) * 100) : '';
        var el = document.getElementById('progress_' + sanitizeId(fname));
        if (el) el.innerText = percent ? percent + '%' : '';
      };
      xhr.onload = function() {
        var url = window.URL.createObjectURL(xhr.response);
        var a = document.createElement('a');
        a.href = url;
        a.download = fname;
        a.click();
        window.URL.revokeObjectURL(url);
      };
      xhr.send();
    }
    function deleteFile(fname) {
      if (confirm('Delete ' + fname + '?')) {
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/delete', true);
        xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        xhr.onload = function() { location.reload(); };
        xhr.send('file=' + encodeURIComponent(fname));
      }
    }
    function renameFile(fname) {
      var newname = prompt('Rename ' + fname + ' to:', fname);
      if (newname && newname !== fname) {
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/rename', true);
        xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        xhr.onload = function() { location.reload(); };
        xhr.send('file=' + encodeURIComponent(fname) + '&newname=' + encodeURIComponent(newname));
      }
    }
    function uploadFile() {
      var fileInput = document.getElementById('fileInput');
      if (fileInput.files.length == 0) { alert('Select a file'); return; }
      var file = fileInput.files[0];
      var formData = new FormData();
      formData.append('upload', file);

      var xhr = new XMLHttpRequest();
      xhr.open('POST', '/upload', true);

      xhr.upload.onprogress = function(event) {
        var percent = event.lengthComputable ? Math.floor((event.loaded / event.total) * 100) : 0;
        document.getElementById('uploadProgress').value = percent;
        document.getElementById('uploadStatus').innerText = percent + '%';
      };
      xhr.onload = function() {
        document.getElementById('uploadStatus').innerText = (xhr.status === 200) ? 'Upload complete' : 'Upload failed';
        setTimeout(function() { location.reload(); }, 1000);
      };
      xhr.send(formData);
    }
    function deleteAll(){
      if (confirm('Delete ALL files from SD?')) {
        var xhr = new XMLHttpRequest();
        xhr.open('POST','/delete_all',true);
        xhr.onload = function(){ alert(xhr.responseText); location.reload(); };
        xhr.send();
      }
    }

    // Fetch con timeout y reintentos
    function fetchWithRetry(url, elementId, retries = 3, timeout = 5000) {
      var attempt = 0;
      function tryFetch() {
        var xhr = new XMLHttpRequest();
        var timeoutId;

        xhr.open('GET', url, true);
        xhr.timeout = timeout;

        xhr.onload = function() {
          clearTimeout(timeoutId);
          if (xhr.status === 200) {
            document.getElementById(elementId).innerText = xhr.responseText;
            document.getElementById(elementId).className = '';
          } else {
            retry();
          }
        };

        xhr.onerror = function() {
          clearTimeout(timeoutId);
          retry();
        };

        xhr.ontimeout = function() {
          clearTimeout(timeoutId);
          retry();
        };

        function retry() {
          attempt++;
          if (attempt < retries) {
            document.getElementById(elementId).innerText = 'Retry ' + attempt + '/' + retries + '...';
            setTimeout(tryFetch, 500);
          } else {
            document.getElementById(elementId).innerText = 'Failed';
            document.getElementById(elementId).className = 'error';
          }
        }

        xhr.send();
      }
      tryFetch();
    }
  </script>
</head>
<body>
  <h1>HIRI-PRO SD File Manager</h1>

  <div class="toolbar">
    <div><b>AP SSID:</b> <span id="ssid" class="loading">Loading...</span> &nbsp; <b>Password:</b> 12345678 &nbsp; <b>IP:</b> <span id="ip" class="loading">Loading...</span></div>
    <div style="margin-top:8px">
      <input type="file" id="fileInput">
      <button onclick="uploadFile()">Upload</button>
      <button onclick="deleteAll()">Delete ALL files</button>
      <progress id="uploadProgress" value="0" max="100"></progress>
      <span id="uploadStatus"></span>
    </div>
  </div>

  <table>
    <tr><th>Name</th><th>Size (bytes)</th><th>Actions</th><th>Progress</th></tr>
)rawliteral";

const char *footerHtml = R"rawliteral(
  </table>
  <script>
    // Cargar IP y SSID con reintentos automáticos
    window.onload = function() {
      fetchWithRetry('/ip', 'ip', 5, 3000);
      fetchWithRetry('/ssid', 'ssid', 5, 3000);
    };
  </script>
</body>
</html>
)rawliteral";

#include <ctype.h>
#include <DNSServer.h>

extern DNSServer dnsServer;

// Replace non-alphanumeric chars by '_' (safe for HTML element ids)
static String sanitizeForId(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    out += (isalnum((unsigned char)c) ? c : '_');
  }
  return out;
}

// Basic filename hardening for SD operations
static bool isSafeFilename(const String &name) {
  if (name.length() == 0 || name.length() > 64) return false;
  if (name.indexOf("..") >= 0) return false;
  if (name.indexOf('\\') >= 0) return false;
  if (name.indexOf(':') >= 0) return false;
  if (name.indexOf("//") >= 0) return false;
  return true;
}

static String normalizeSdPath(String name) {
  name.trim();
  if (!name.startsWith("/")) name = "/" + name;
  return name;
}

static bool ensureWifiAuth() {
  // WPA2-PSK requires >= 8 chars. If invalid, do not start AP.
  return String(AP_PASSWORD).length() >= 8;
}

// ---- Routes implementation ----
void listFiles() {
  // Enviar header primero
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(headerHtml);

  // Iterar archivos y enviar cada fila
  File root = SD.open("/");
  if (root) {
    File file = root.openNextFile();
    int fileCount = 0;
    while (file && fileCount < 100) { // Límite de seguridad
      if (!file.isDirectory()) {
        String name = file.name();
        size_t fsize = file.size();
        file.close();

        String safeId = sanitizeForId(name);
        String row = "<tr>";
        row += "<td>" + name + "</td>";
        row += "<td>" + String(fsize) + "</td>";
        row += "<td>";
        row += "<button onclick=\"downloadFile('" + name +
               "')\">Download</button>";
        row += "<button onclick=\"deleteFile('" + name + "')\">Delete</button>";
        row += "<button onclick=\"renameFile('" + name + "')\">Rename</button>";
        row += "</td>";
        row += "<td><span id='progress_" + safeId + "'></span></td>";
        row += "</tr>\n";

        server.sendContent(row);
        fileCount++;
      } else {
        file.close();
      }
      file = root.openNextFile();
      yield(); // Dar tiempo al watchdog
    }
    root.close();
  }

  // Enviar footer
  server.sendContent(footerHtml);
  server.sendContent(""); // Finalizar chunked encoding
}

void handleFileDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "File not specified");
    return;
  }
  String filename = normalizeSdPath(server.arg("file"));
  if (!isSafeFilename(filename)) {
    server.send(400, "text/plain", "Invalid filename");
    return;
  }
  if (!SDOK) {
    server.send(503, "text/plain", "SD unavailable");
    return;
  }

  File f = SD.open(filename);
  if (!f) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"" + filename.substring(1) + "\"");
  server.sendHeader("Content-Length", String(f.size()));
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleFileDelete() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "File not specified");
    return;
  }
  String filename = normalizeSdPath(server.arg("file"));
  if (!isSafeFilename(filename)) {
    server.send(400, "text/plain", "Invalid filename");
    return;
  }
  if (!SDOK) {
    server.send(503, "text/plain", "SD unavailable");
    return;
  }
  bool ok = SD.remove(filename);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Delete failed");
}

void handleFileRename() {
  if (!server.hasArg("file") || !server.hasArg("newname")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  String oldname = normalizeSdPath(server.arg("file"));
  String newname = normalizeSdPath(server.arg("newname"));
  if (!isSafeFilename(oldname) || !isSafeFilename(newname)) {
    server.send(400, "text/plain", "Invalid filename");
    return;
  }
  if (!SDOK) {
    server.send(503, "text/plain", "SD unavailable");
    return;
  }
  bool ok = SD.rename(oldname, newname);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Rename failed");
}

extern File uploadFile;

void handleFileUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!SDOK) {
      server.send(503, "text/plain", "SD unavailable");
      return;
    }
    String filename = normalizeSdPath(upload.filename);
    if (!isSafeFilename(filename)) {
      server.send(400, "text/plain", "Invalid filename");
      return;
    }
    uploadFile = SD.open(filename, FILE_WRITE);
    if (!uploadFile) {
      server.send(500, "text/plain", "Open failed");
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t w = uploadFile.write(upload.buf, upload.currentSize);
      if (w != upload.currentSize) {
        uploadFile.close();
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    server.send(500, "text/plain", "Upload aborted");
  }
}

void handleDeleteAll() {
  if (!SDOK) {
    server.send(503, "text/plain", "SD unavailable");
    return;
  }
  File root = SD.open("/");
  if (!root) {
    server.send(500, "text/plain", "SD open error");
    return;
  }

  String files[256];
  int n = 0;
  File file = root.openNextFile();
  while (file && n < 256) {
    if (!file.isDirectory()) {
      String name = file.name();
      files[n++] = normalizeSdPath(name);
    }
    file.close();
    file = root.openNextFile();
    yield();
  }
  root.close();

  int okCnt = 0, failCnt = 0;
  for (int i = 0; i < n; ++i) {
    if (isSafeFilename(files[i]) && SD.remove(files[i])) okCnt++;
    else failCnt++;
    yield();
  }

  server.send(200, "text/plain",
              "Deleted: " + String(okCnt) + ", Failed: " + String(failCnt));
}

void handleIp() { server.send(200, "text/plain", WiFi.softAPIP().toString()); }

void handleSsid() { server.send(200, "text/plain", AP_SSID_STR); }

void setupWifiRoutes() {
  server.on("/", HTTP_GET, listFiles);
  server.on("/download", HTTP_GET, handleFileDownload);
  server.on("/delete", HTTP_POST, handleFileDelete);
  server.on("/rename", HTTP_POST, handleFileRename);
  server.on(
      "/upload", HTTP_POST, []() { server.send(200, "text/plain", "OK"); },
      handleFileUpload);
  server.on("/delete_all", HTTP_POST, handleDeleteAll);
  server.on("/ip", HTTP_GET, handleIp);
  server.on("/ssid", HTTP_GET, handleSsid);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
}

//-----------handrles start/stop wifi server
void startWifiApServer() {
  // Avoid duplicate start
  if (wifiModeActive) return;

  // Validate AP credentials
  if (!ensureWifiAuth()) {
    Serial.println("[WiFi][ERR] Invalid AP password (min 8 chars)");
    oledStatus("WIFI MODE", "ERROR", "PASS TOO SHORT", "MIN 8 CHARS");
    return;
  }

  // Mount SD first: file manager is useless/insecure without SD
  if (!SDOK) {
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);
    Serial.println(SDOK ? "[SD] Ready" : "[SD] FAIL");
  }
  if (!SDOK) {
    oledStatus("WIFI MODE", "ERROR", "SD NOT READY", "CANNOT START");
    return;
  }

  // Clean WiFi state
  server.stop();
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(120);

  WiFi.mode(WIFI_AP);

  // Stable AP network to reduce startup issues
  IPAddress local(192, 168, 4, 1);
  IPAddress gw(192, 168, 4, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(local, gw, mask);

  bool apOk = WiFi.softAP(AP_SSID_STR.c_str(), AP_PASSWORD, 1, false, 2);
  if (!apOk) {
    Serial.println("[WiFi][ERR] softAP start failed");
    oledStatus("WIFI MODE", "ERROR", "AP START FAIL", "RETRY");
    return;
  }

  // Iniciar DNS Server para Portal Cautivo (Redirige todo a nuestra IP)
  dnsServer.start(53, "*", WiFi.softAPIP());

  setupWifiRoutes();
  server.begin();
  wifiModeActive = true;

  IPAddress ip = WiFi.softAPIP();
  apIpStr = ip.toString();
  Serial.print("[WiFi] AP SSID: ");
  Serial.println(AP_SSID_STR);
  Serial.print("[WiFi] AP IP: ");
  Serial.println(ip);
  
  // No llamamos oledStatus aqui porque ui.ino manejará la pantalla dedicada
}

void stopWifiApServer() {
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiModeActive = false;
  Serial.println("[WiFi] AP stopped");
}

