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
const char *DEVICE_ID_STR = "10"; // Se actualizara desde config o manualmente
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
