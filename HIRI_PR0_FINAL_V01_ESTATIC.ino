/*
 * FirmwarePro.ino
 * Firmware HIRI PR0 estatico: sensores, GNSS, SD, UI OLED y telemetria HTTP.
 *
 * Envio HTTP vigente:
 *   API_BASE = http://api-sensores.cmasccp.cl/insertarMedicion
 *   DEVICE_ID_STR selecciona la lista fija de idsSensores en getIdsSensores().
 *   GLOBAL_IDS_VARIABLES es comun a todos los dispositivos soportados.
 *   sendCurrentMeasurement() arma valores en el mismo orden de esas listas.
 *
 * Orden del payload valores:
 *   1  SO2 / gas
 *   2  TVOC
 *   3  eCO2
 *   4  Latitud
 *   5  Longitud
 *   6  CSQ
 *   7  Velocidad km/h
 *   8  Satelites
 *   9  Bateria V
 *   10 PMS temperatura
 *   11 PMS humedad
 *   12 PM1.0
 *   13 PM2.5
 *   14 PM10
 *   15 PM100 SDS198
 *   16 SHT temperatura
 *   17 SHT humedad
 *
 * Los datos faltantes o invalidos se envian como "-1" para evitar romper la URL.
 */
// --------------------LIBRARY SENSORS, DEFINES & GLOBALS --------------------
#include "config.h"
// Modem definition moved to config.h
// librerias de sensores
#include "Adafruit_SHT31.h"// temperatura externa utalca
#include "Adafruit_SHT4x.h"// temperatura externa nuestro
//Adafruit_SHT4x sht4 = Adafruit_SHT4x();//se ordena mas abajo
#include "DFRobot_MultiGasSensor.h" //gas
#define I2C_ADDRESS 0x74 //direccion con 0 en todos los pines por dicereccion
DFRobot_GAS_I2C gas(&Wire, I2C_ADDRESS); //multigas
#include <DFRobot_ENS160.h> //ambient
DFRobot_ENS160_I2C ENS160(&Wire, /*I2CAddr*/ 0x53); //sensor voc

#include "FS.h"
#include "SD.h"
#include "SPI.h"
// #include <Adafruit_NeoPixel.h> // Moved to config.h
// #include <Arduino.h> // Included in config.h
#include <Preferences.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
// #include <TinyGsmClient.h> // Moved to config.h
#include <DNSServer.h> // Added for Captive Portal
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_task_wdt.h>

// Modem modem
// #define TINY_GSM_RX_BUFFER 4096 // Moved to config.h
#define SerialAT Serial1
TinyGsm modem(SerialAT);

// --- Missing global constants/state restored for broken build ---
#define RTC_PROBE_PERIOD_MS 60000
#define RTC_SYNC_THRESHOLD 30
#define MIN_VALID_EPOCH 1672531200 // 2023-01-01

// SDS198 protocol constants
const byte HEADER = 0xAA;
const byte CMD = 0xCF;
const byte TAIL = 0xAB;

// Firmware version
String VERSION = "Pro V0.1.12V";

// Global states of sensors and RTC
bool rtcOK = false;
bool SHT31OK = false;
bool SHT4xOK = false;
bool SDS198OK = false;
bool GasOK = false;
bool ENS160OK = false;
bool SDOK = false;
bool wifiModeActive = false;
bool hasRed = false;

// Config Instance
SystemConfig config;

// Objects configuracion de sensores y pantalla
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SHT31 sht31 = Adafruit_SHT31(); //utal
Adafruit_SHT4x sht4 = Adafruit_SHT4x();  //nuestro
RTC_DS3231 rtc;
Preferences prefs;
SPIClass spiSD(HSPI);
WebServer server(80); // Used in wifi.ino
DNSServer dnsServer;  // Captive Portal DNS

// PMS
SoftwareSerial pms(pms_TX, pms_RX);

// Button flags (edge + debounce)
// Button flags (edge + debounce)
volatile bool btn1ClickFlag = false;
volatile bool btn2ClickFlag = false;
volatile bool btn2HoldFlag = false;

// Internal Flags for ISR State tracking
// (Removed complex hold state tracking)

// Debounce Tracking
volatile uint32_t lastDebounceTime1 = 0;
volatile uint32_t lastDebounceTime2 = 0; 
const uint32_t BTN1_DEBOUNCE_MS = 80;   // Bajado para velocidad normal de navegación
const uint32_t BTN2_DEBOUNCE_MS = 80;   // Uniforme con el botón 1

// Data Variables
uint16_t PM1 = 0, PM25 = 0, PM10 = 0;
float pmsTempC = NAN, pmsHum = NAN;
int SDS198PM100 = 0;
float tempsht31 = NAN, humsht31 = NAN; //utal
float tempsht4x = NAN, humsht4x = NAN; //nuestro
float rtcTempC = NAN;
float batV = 0.0f;
int csq = 0;
bool networkError = false;

// GPS Data
String gpsLat = "NaN", gpsLon = "NaN";
String gpsTime = "N/A", gpsDate = "N/A";
String satellitesStr = "0", hdopStr = "N/A", gpsAlt = "N/A";
String gpsStatus = "NoFix";
String gpsSpeedKmh = "0.0";

// Internal Logic Variables
bool loggingEnabled = false;
bool streaming = false;

// UI Rotation (Modo Debug)
uint8_t debugScreenIndex = 0;
uint32_t lastDebugRotationMs = 0;
const uint32_t DEBUG_ROTATION_INTERVAL_MS = 10000;

String csvFileName = "";
String logFilePath = "";
String failedTxPath = "";
String currentNote = "9"; // Global note for one-shot logging
// Variables moved to main for centralization
String lastSavedCSVLine = ""; // Used in sd_card.ino for OLED display
File uploadFile;              // Used in wifi.ino for file uploads

String deviceID = "/HIRIPV";
const char *DEVICE_ID_STR = "10"; // ID del dispositivo actual "1" es el modelo estatico para valpo es la nueva lista
String AP_SSID_STR = "";
const char *AP_PASSWORD = "12345678";
String apIpStr = "0.0.0.0";
// -------------------- Measurements API (real endpoint) --------------------
const char *API_BASE = "http://api-sensores.cmasccp.cl/insertarMedicion";
const char *GLOBAL_IDS_VARIABLES = "53,54,55,11,12,15,45,46,4,3,6,7,8,9,51,3,6";

// APN
const char apn[] = "flolive.net"; //  "flolive.net"; nuevo apn const char apn[] = "gigsky-02"; 
const char gprsUser[] = "";
const char gprsPass[] = "";

// Counters
uint32_t sendCounter = 0;
uint32_t sdSaveCounter = 0;
// Separamos guardado SD y transmisión HTTP con timers independientes.
uint32_t lastHttpSend = 0;
uint32_t lastSdSave = 0;
// Estado de actividad para UI (header U/S).
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

// Watchdog
#define WDT_TIMEOUT 60
String rebootReason = "Unknown";
String networkOperator = "N/A";
String networkTech = "N/A";
String signalQuality = "0";
String registrationStatus = "N/A";

// XTRA
uint32_t lastXtraDownload = 0;
bool xtraSupported = false;
bool xtraLastOk = false;
const uint32_t XTRA_REFRESH_MS = 3UL * 24UL * 60UL * 60UL * 1000UL;
const uint32_t NETWORK_RESTART_INTERVAL_MS = 2UL * 60UL * 60UL * 1000UL;
const uint32_t NETWORK_WATCHDOG_CHECK_MS = 60000UL;
const uint8_t MAX_MODEM_RECOVERY_ATTEMPTS = 3;
const uint32_t MODEM_INIT_TOTAL_TIMEOUT_MS = 90000UL;
const uint32_t MODEM_BOOT_SETTLE_MS = 3000UL;
const uint32_t HEALTH_LOG_INTERVAL_MS = 300000UL;
const uint8_t ENS160_INVALID_REINIT_THRESHOLD = 3;
const uint8_t SHT4X_FAIL_REINIT_THRESHOLD = 3;
const uint8_t HTTP_FAIL_MAX_CONSECUTIVE = 6;
const uint32_t HTTP_RETRY_BACKOFF_MS = 60UL * 60UL * 1000UL;
uint32_t networkDownSinceMs = 0;
uint32_t lastNetworkWatchdogCheckMs = 0;
uint8_t modemRecoveryAttempts = 0;
uint32_t lastHealthLogMs = 0;
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

// GnssDbgState moved to gps.ino

// SD Definition constants
const int SD_SCLK = 14, SD_MISO = 2, SD_MOSI = 15, SD_CS = 13;

// Extern function declarations (if needed explicitly, though linking usually
// handles it)
void loadConfig();
void saveConfig();
void applyLEDConfig();
void writeErrorLogHeader();
String generateCSVFileName();
void writeCSVHeader();
void drawAnimation(); // From animacion.ino
void startWifiApServer();
void stopWifiApServer();
void renderDisplay(); // From ui.ino
bool saveCSVData();
void checkRebootReason();
void readPMS();
bool readFrameSDS198(byte *buf);
void updatePmLed(float pm25);
void gnssBringUp();
void gnssDiagTick();
void gnssDebugPollAsync();
void gnssWatchdog();
bool atTick(bool &done, bool &ok);
bool atRun(const String &cmd, const String &expect1 = "OK",
           const String &expect2 = "ERROR", uint32_t timeout_ms = 8000);
bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms = 4000);
bool httpGet_webhook(const String &url);
bool detectAndEnableXtra();
bool downloadXtraOnce();
void downloadXtraIfDue();
void parseNMEA(const String &line);
void saveFailedTransmission(const String &url, const String &errorType);
bool sendCurrentMeasurement();
void logError(const String &type, const String &ctx, const String &msg);
void handleButtonLogic(); // Renamed from dispatchButtonFlags
void showMessage(const char *msg); // From ui.ino
extern bool uiFullMode; // From ui.ino
bool initModemWithRecovery();
void modemHardPowerOffSequence();
void modemColdBootSequence();
bool modemWaitForAT(uint32_t totalTimeoutMs);
void modemBlinkBlueStep(bool on, uint8_t level = 80);
bool modemInitAttempt();
void networkRestartWatchdogTick();
void refreshHealthLog();
void modemPowerOffForReset();
bool recoverModemForHttpFailures();
void httpBackoffTick();
void handleHttpFailureState(const char *reason);
void recoverI2CBus(const char *reason);
void initI2CSensors();
void updateEns160State();

// ISR Function Prototypes
void IRAM_ATTR isr_btn1();
void IRAM_ATTR isr_btn2();

// UI Event Handlers
extern void ui_btn1_click();
extern void ui_btn2_click();

// Oled Status Helper (used by wifi/main)
// Renderiza un estado rápido en OLED con hasta 4 líneas de texto.
// Se usa para feedback de arranque, red, módem y operaciones críticas.
void oledStatus(const String &l1, const String &l2 = "", const String &l3 = "",
                const String &l4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 12);
  u8g2.print(l1);
  u8g2.setCursor(0, 26);
  u8g2.print(l2);
  u8g2.setCursor(0, 40);
  u8g2.print(l3);
  u8g2.setCursor(0, 54);
  u8g2.print(l4);
  u8g2.sendBuffer();
}

// AT Helper (needed in main)
// Inicia una sesión AT no bloqueante, guarda expectativas y timeout.
// La respuesta se procesa luego con atTick() para no congelar el loop.
void atBegin(const String &cmd, const String &expect1, const String &expect2,
             uint32_t timeout_ms) {
  modem.stream.print("AT");
  modem.stream.println(cmd);
  at.active = true;
  at.expect1 = expect1;
  at.expect2 = expect2;
  at.resp = "";
  at.deadline = millis() + timeout_ms;
}

// Avanza la máquina de estados AT leyendo serial y detectando fin/timeout.
// También enruta tramas NMEA entrantes al parser GNSS cuando aparecen.
bool atTick(bool &done, bool &ok) {
  while (SerialAT.available()) {
    String line = SerialAT.readStringUntil('\n');
    line.trim();
    if (line.isEmpty())
      continue;

    if (line.charAt(0) == '$') {
      parseNMEA(line);
      continue;
    }

    if (!at.active)
      continue;
    at.resp += line;
    at.resp += "\n";
    if (at.expect1.length() && line.indexOf(at.expect1) >= 0) {
      done = true;
      ok = true;
      at.active = false;
      return true;
    }
    if (at.expect2.length() && line.indexOf(at.expect2) >= 0) {
      done = true;
      ok = (at.expect2 == "OK");
      at.active = false;
      return true;
    }
  }
  if (at.active && millis() > at.deadline) {
    done = true;
    ok = false;
    at.active = false;
    return true;
  }
  done = false;
  ok = false;
  return false;
}

// Ejecuta un comando AT de forma bloqueante hasta éxito, error o timeout.
// Es un helper práctico para setup y tareas puntuales de configuración.
bool atRun(const String &cmd, const String &expect1, const String &expect2,
           uint32_t timeout_ms) {
  atBegin(cmd, expect1, expect2, timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok))
      break;
    delay(1);
  }
  return ok;
}

// Envía AT y devuelve la respuesta completa en un String para diagnóstico.
// Útil cuando se necesita parsear contenido (no solo OK/ERROR).
bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms) {
  atBegin(cmd, "OK", "ERROR", timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok))
      break;
    delay(1);
  }
  resp = at.resp;
  return ok;
}

// Consulta operador, tecnología y registro de red desde el módem.
// Actualiza variables globales usadas en UI, logs y comandos seriales.
void updateNetworkInfo() {
  String resp;
  if (sendAtSync("+COPS?", resp, 3000)) {
    int idx = resp.indexOf("+COPS:");
    if (idx >= 0) {
      int start = resp.indexOf('"', idx);
      int end = resp.indexOf('"', start + 1);
      if (start >= 0 && end > start) {
        networkOperator = resp.substring(start + 1, end);
      }
    }
  }
  if (sendAtSync("+COPS?", resp, 3000)) {
    if (resp.indexOf(",7") >= 0)
      networkTech = "LTE";
    else if (resp.indexOf(",2") >= 0)
      networkTech = "3G";
    else if (resp.indexOf(",0") >= 0)
      networkTech = "2G";
    else
      networkTech = "Unknown";
  }
  signalQuality = String(csq);
  if (sendAtSync("+CREG?", resp, 2000)) {
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0)
      registrationStatus = "Registered";
    else if (resp.indexOf(",2") >= 0)
      registrationStatus = "Searching";
    else
      registrationStatus = "NotRegistered";
  }
}

bool modemWaitForAT(uint32_t totalTimeoutMs) {
  uint32_t start = millis();
  uint32_t attempt = 0;
  bool blinkOn = false;
  oledStatus("MODEM", "AT wait...");
  while (millis() - start < totalTimeoutMs) {
    esp_task_wdt_reset();
    attempt++;
    strncpy(currentCriticalStage, "modem_testAT", sizeof(currentCriticalStage) - 1);
    currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
    blinkOn = !blinkOn;
    modemBlinkBlueStep(blinkOn, 60);
    if (modem.testAT(1000)) {
      modemBlinkBlueStep(true, 100);
      return true;
    }
    Serial.printf("[MODEM] testAT retry %lu\n", (unsigned long)attempt);
    oledStatus("MODEM", "AT wait...", String(attempt));
    delay(300);
  }
  logError("MODEM_AT_TIMEOUT", "modemWaitForAT",
           "testAT timeout during modem init");
  return false;
}

void modemBlinkBlueStep(bool on, uint8_t level) {
  pixels.setPixelColor(0, on ? pixels.Color(0, 0, level) : pixels.Color(0, 0, 0));
  pixels.show();
}

void modemHardPowerOffSequence() {
  strncpy(currentCriticalStage, "modem_poweroff",
          sizeof(currentCriticalStage) - 1);
  currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
  oledStatus("MODEM", "Power OFF");
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_FLIGHT, OUTPUT);
  pinMode(MODEM_DTR, OUTPUT);

  digitalWrite(MODEM_FLIGHT, HIGH);
  digitalWrite(MODEM_DTR, LOW);
  modemBlinkBlueStep(true, 40);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(250);
  modemBlinkBlueStep(false, 40);
  delay(250);
  modemBlinkBlueStep(true, 40);
  delay(250);
  modemBlinkBlueStep(false, 40);
  delay(250);
  delay(1800);
  digitalWrite(MODEM_PWRKEY, LOW);
  modemBlinkBlueStep(true, 40);
  delay(250);
  modemBlinkBlueStep(false, 40);
  delay(2500);
  modemBlinkBlueStep(false, 40);
}

void modemColdBootSequence() {
  // Always force the modem fully off first so boot/recovery starts from a known state.
  modemHardPowerOffSequence();

  strncpy(currentCriticalStage, "modem_cold_boot",
          sizeof(currentCriticalStage) - 1);
  currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
  oledStatus("MODEM", "Cold boot");
  digitalWrite(MODEM_FLIGHT, HIGH);
  digitalWrite(MODEM_DTR, LOW);
  modemBlinkBlueStep(true, 100);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(200);
  modemBlinkBlueStep(false, 100);
  delay(200);
  modemBlinkBlueStep(true, 100);
  delay(200);
  modemBlinkBlueStep(false, 100);
  delay(200);
  modemBlinkBlueStep(true, 100);
  delay(1200);
  digitalWrite(MODEM_PWRKEY, LOW);
  oledStatus("MODEM", "Boot settle");
  modemBlinkBlueStep(false, 100);
  delay(250);
  modemBlinkBlueStep(true, 100);
  delay(250);
  modemBlinkBlueStep(false, 100);
  delay(250);
  delay(MODEM_BOOT_SETTLE_MS);
  modemBlinkBlueStep(true, 100);
}

bool modemInitAttempt() {
  oledStatus("MODEM", "Cold boot...");
  modemColdBootSequence();

  if (!modemWaitForAT(MODEM_INIT_TOTAL_TIMEOUT_MS)) {
    return false;
  }

  oledStatus("MODEM", "Config...");
  strncpy(currentCriticalStage, "modem_setup", sizeof(currentCriticalStage) - 1);
  currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
  if (!atRun("+CEDRXS=0", "OK", "ERROR", 1500)) {
    logError("MODEM_CFG_FAIL", "CEDRXS", "Failed to disable eDRX");
    return false;
  }
  if (!atRun("+CPSMS=0", "OK", "ERROR", 1500)) {
    logError("MODEM_CFG_FAIL", "CPSMS", "Failed to disable PSM");
    return false;
  }

  oledStatus("NET", "Attach/PDP...");
  strncpy(currentCriticalStage, "wait_network", sizeof(currentCriticalStage) - 1);
  currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
  if (!modem.waitForNetwork(60000)) {
    logError("MODEM_NET_FAIL", "waitForNetwork", "Attach timeout");
    oledStatus("NET", "Attach FAIL");
    return false;
  }

  strncpy(currentCriticalStage, "gprs_connect", sizeof(currentCriticalStage) - 1);
  currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    logError("MODEM_PDP_FAIL", "gprsConnect", "PDP connect failed");
    oledStatus("NET", "PDP FAIL");
    return false;
  }

  modemRecoveryAttempts = 0;
  hasRed = true;
  oledStatus("NET", "PDP OK");
  return true;
}

bool initModemWithRecovery() {
  for (uint8_t attempt = 1; attempt <= MAX_MODEM_RECOVERY_ATTEMPTS; ++attempt) {
    Serial.printf("[MODEM] Init attempt %u/%u\n", attempt,
                  MAX_MODEM_RECOVERY_ATTEMPTS);
    if (modemInitAttempt()) {
      return true;
    }
    modemRecoveryAttempts = attempt;
    char attemptBuf[8];
    snprintf(attemptBuf, sizeof(attemptBuf), "%u", attempt);
    oledStatus("MODEM", "RECOVERY", attemptBuf);
    delay(800);
  }

  logError("MODEM_INIT_FATAL", "initModemWithRecovery",
           "Modem init failed after recovery attempts");
  return false;
}

void initI2CSensors() {
  if (!sht4.begin()) {
    SHT4xOK = false;
    logError("I2C_SENSOR_FAIL", "SHT4X.begin", "SHT4x not found");
  } else {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    SHT4xOK = true;
    sht4xFailCount = 0;
  }

  if (!sht31.begin(0x44)) {
    SHT31OK = false;
    logError("I2C_SENSOR_FAIL", "SHT31.begin", "SHT31 not found");
  } else {
    SHT31OK = true;
  }

  if (NO_ERR != ENS160.begin()) {
    ENS160OK = false;
    ens160DataValid = false;
    logError("I2C_SENSOR_FAIL", "ENS160.begin", "ENS160 init failed");
  } else {
    ENS160.setPWRMode(ENS160_STANDARD_MODE);
    ENS160OK = true;
    ens160InvalidCount = 0;
  }
}

void recoverI2CBus(const char *reason) {
  logError("I2C_RECOVER", "recoverI2CBus", reason);
  strncpy(currentCriticalStage, "i2c_recover", sizeof(currentCriticalStage) - 1);
  currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
  Wire.end();
  delay(50);
  Wire.begin();
  Wire.setTimeOut(50);
  Wire.setClock(100000);
  initI2CSensors();
}

void updateEns160State() {
  if (!ENS160OK) {
    ens160DataValid = false;
    return;
  }

  strncpy(currentCriticalStage, "ens160_read", sizeof(currentCriticalStage) - 1);
  currentCriticalStage[sizeof(currentCriticalStage) - 1] = '\0';
  if (!isnan(pmsTempC) && !isnan(pmsHum)) {
    ENS160.setTempAndHum(/*temperature=*/pmsTempC, /*humidity=*/pmsHum);
  }
  ens160StatusRaw = ENS160.getENS160Status();
  ens160Aqi = ENS160.getAQI();
  ens160Tvoc = ENS160.getTVOC();
  ens160Eco2 = ENS160.getECO2();

  bool statusOk = (ens160StatusRaw <= 0x02);
  bool valuesOk =
      (ens160Aqi >= 1U && ens160Aqi <= 5U && ens160Tvoc <= 65000U &&
       ens160Eco2 >= 400U && ens160Eco2 <= 65000U);
  ens160DataValid = statusOk && valuesOk;

  if (ens160DataValid) {
    ens160InvalidCount = 0;
    return;
  }

  if (ens160StatusRaw == 0x01 || ens160StatusRaw == 0x02) {
    return;
  }

  ens160InvalidCount++;
  logError("ENS160_INVALID", "updateEns160State",
           "Invalid ENS160 status/data detected");
  if (ens160InvalidCount >= ENS160_INVALID_REINIT_THRESHOLD) {
    ens160InvalidCount = 0;
    recoverI2CBus("ENS160 invalid threshold");
  }
}

void networkRestartWatchdogTick() {
  const uint32_t now = millis();
  if (now - lastNetworkWatchdogCheckMs < NETWORK_WATCHDOG_CHECK_MS) {
    return;
  }
  lastNetworkWatchdogCheckMs = now;

  if (wifiModeActive) {
    networkDownSinceMs = 0;
    return;
  }

  bool netOk = modem.isNetworkConnected();
  bool pdpOk = modem.isGprsConnected();
  bool cellularOk = netOk && pdpOk;
  hasRed = cellularOk;

  if (cellularOk) {
    if (networkDownSinceMs != 0) {
      Serial.println("[NET][WD] Connectivity restored");
    }
    networkDownSinceMs = 0;
    return;
  }

  if (networkDownSinceMs == 0) {
    networkDownSinceMs = now;
    Serial.println("[NET][WD] Connectivity lost, starting 2h timer");
    return;
  }

  uint32_t downMs = now - networkDownSinceMs;
  Serial.printf("[NET][WD] Connectivity down for %lu s\n",
                (unsigned long)(downMs / 1000UL));

  if (downMs >= 600000UL &&
      modemRecoveryAttempts < MAX_MODEM_RECOVERY_ATTEMPTS) {
    modemRecoveryAttempts++;
    logError("MODEM_RECOVERY", "networkRestartWatchdogTick",
             "Attempting modem recovery after prolonged connectivity loss");
    if (initModemWithRecovery()) {
      networkDownSinceMs = 0;
      modemRecoveryAttempts = 0;
      return;
    }
  }

  if (downMs >= NETWORK_RESTART_INTERVAL_MS) {
    logError("NET_WATCHDOG_RESTART", "networkRestartWatchdogTick",
             "No cellular connectivity for 2h");
    Serial.println("[NET][WD] Restarting after 2h without connectivity");
    delay(100);
    ESP.restart();
  }
}

void refreshHealthLog() {
  const uint32_t now = millis();
  if (now - lastHealthLogMs < HEALTH_LOG_INTERVAL_MS) {
    return;
  }
  lastHealthLogMs = now;

  char msg[128];
  snprintf(msg, sizeof(msg),
           "heap=%lu minHeap=%lu stage=%s net=%d pdp=%d ens=%u/%u/%u status=%u",
           (unsigned long)ESP.getFreeHeap(),
           (unsigned long)ESP.getMinFreeHeap(), currentCriticalStage,
           modem.isNetworkConnected() ? 1 : 0, modem.isGprsConnected() ? 1 : 0,
           (unsigned int)ens160Aqi, (unsigned int)ens160Tvoc,
           (unsigned int)ens160Eco2, (unsigned int)ens160StatusRaw);
  logError("HEALTH", "refreshHealthLog", msg);
}

void modemPowerOffForReset() {
  Serial.println("[MODEM] Powering off before ESP restart");

  bool offOk = modem.poweroff();
  if (!offOk) {
    logError("MODEM_POWEROFF_FAIL", "modemPowerOffForReset",
             "TinyGSM poweroff failed, forcing hard shutdown by pins");
  }

  Serial.println("[MODEM] Forcing hard shutdown by pins");
  modemHardPowerOffSequence();
}

bool recoverModemForHttpFailures() {
  logError("HTTP_MODEM_RECOVERY", "recoverModemForHttpFailures",
           "Starting hard modem recovery after consecutive HTTP failures");
  Serial.println("[HTTP][WD] Starting hard modem recovery");
  modemPowerOffForReset();
  delay(500);
  hasRed = false;
  return initModemWithRecovery();
}

void httpBackoffTick() {
  if (!httpBackoffActive) {
    return;
  }

  const uint32_t now = millis();
  if ((int32_t)(now - httpBackoffUntilMs) < 0) {
    return;
  }

  httpBackoffActive = false;
  httpBackoffUntilMs = 0;
  httpConsecutiveFailCount = 0;
  logError("HTTP_BACKOFF_END", "httpBackoffTick",
           "HTTP backoff ended, allowing transmissions again");
  Serial.println("[HTTP][BACKOFF] 1h elapsed, HTTP retries re-enabled");
}

void handleHttpFailureState(const char *reason) {
  if (httpConsecutiveFailCount < 255) {
    httpConsecutiveFailCount++;
  }

  if (httpConsecutiveFailCount < HTTP_FAIL_MAX_CONSECUTIVE) {
    return;
  }

  char msg[128];
  snprintf(msg, sizeof(msg), "HTTP failed %u consecutive times (%s)",
           (unsigned int)httpConsecutiveFailCount, reason);
  logError("HTTP_FAIL_STREAK", "handleHttpFailureState", msg);

  if (recoverModemForHttpFailures()) {
    Serial.println("[HTTP][WD] Modem recovered, HTTP retries continue");
    httpConsecutiveFailCount = 0;
    return;
  }

  httpBackoffActive = true;
  httpBackoffUntilMs = millis() + HTTP_RETRY_BACKOFF_MS;
  logError("HTTP_BACKOFF_START", "handleHttpFailureState",
           "Modem recovery failed, pausing HTTP for 1 hour");
  Serial.println("[HTTP][BACKOFF] Pausing HTTP for 1 hour");
}

// -------------------- Telemetry Tx --------------------
// NOTA DE INTEGRACION:
// - "streaming" controla transmisión HTTP.
// - "loggingEnabled" controla guardado en SD.
// - Ambos están separados a propósito para evitar acoplar guardar/transmitir.
// Construye payload/URL de medición según hardware activo y envía por HTTP.
// Persiste contadores en flash y registra fallos en SD cuando corresponde.
extern bool SHT31OK, SHT4xOK, SDS198OK, GasOK, ENS160OK;
extern int SDS198PM100;
extern float tempsht31, humsht31, tempsht4x, humsht4x;
extern DFRobot_GAS_I2C gas;
extern DFRobot_ENS160_I2C ENS160;

// idsSensores vigentes por dispositivo. Cada lista debe tener 17 posiciones y
// mantener el mismo orden que GLOBAL_IDS_VARIABLES y los v1..v17 del payload.
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
  String idsSensores = getIdsSensores(String(DEVICE_ID_STR));
  if (idsSensores == "") {
      Serial.println("[HTTP] Error: No idsSensores configured for this DEVICE_ID.");
      return false; 
  }

  String v1 = GasOK ? safeFloatStr(gas.readGasConcentrationPPM()) : missingUrlValue();
  String v2 = (ENS160OK && ens160DataValid) ? safeIntStr(ens160Tvoc) : missingUrlValue();
  String v3 = (ENS160OK && ens160DataValid) ? safeIntStr(ens160Eco2) : missingUrlValue();
  String v4 = safeGpsStr(gpsLat);
  String v5 = safeGpsStr(gpsLon);
  String v6 = safeIntStr(csq);
  String v7 = safeGpsStr(gpsSpeedKmh);
  String v8 = safeSatsStr(satellitesStr);
  String v9 = safeFloatStr(batV);
  String v10 = isnan(pmsTempC) ? missingUrlValue() : safeFloatStr(pmsTempC);
  String v11 = isnan(pmsHum) ? missingUrlValue() : safeFloatStr(pmsHum);
  String v12 = safeUIntStr(PM1);
  String v13 = safeUIntStr(PM25);
  String v14 = safeUIntStr(PM10);
  String v15 = SDS198OK ? safeUIntStr(SDS198PM100) : missingUrlValue();

  float tSht = SHT4xOK ? tempsht4x : (SHT31OK ? tempsht31 : NAN);
  float hSht = SHT4xOK ? humsht4x : (SHT31OK ? humsht31 : NAN);
  String v16 = isnan(tSht) ? missingUrlValue() : safeFloatStr(tSht);
  String v17 = isnan(hSht) ? missingUrlValue() : safeFloatStr(hSht);

  String valores = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 + "," + v6 + "," + v7 + "," + v8 + "," + v9 + "," + 
                   v10 + "," + v11 + "," + v12 + "," + v13 + "," + v14 + "," + v15 + "," + v16 + "," + v17;

  String fullUrl = String(API_BASE) + "?idsSensores=" + idsSensores + 
                   "&idsVariables=" + GLOBAL_IDS_VARIABLES + "&valores=" + valores;

  Serial.println("[HTTP] GET " + fullUrl);
  if (httpGet_webhook(fullUrl)) {
    sendCounter++;
    httpConsecutiveFailCount = 0;
    prefs.begin("system", false);
    prefs.putUInt("sendCnt", sendCounter);
    prefs.end();
    Serial.println("[HTTP] OK");
    return true;
  }

  handleHttpFailureState("http_send_fail");

  saveFailedTransmission(fullUrl, "HTTP_FAIL");
  Serial.println("[HTTP] FAIL");
  return false;
}

// Poll de botones por flags con debounce por software.
// -------------------- INTERRUPT SERVICE ROUTINES --------------------
// Button 2: Simple Click (FALLING/RISING depending on user)
void IRAM_ATTR isr_btn2() {
  uint32_t now = millis();
  // Debounce check
  if (now - lastDebounceTime2 > BTN2_DEBOUNCE_MS) {
    lastDebounceTime2 = now;
    btn2ClickFlag = true;
  }
}

// Checks logic (Simplified)
void handleButtonLogic() {
  // Software polling for Button 1 (Active LOW, falling edge simulation)
  // Handles extreme contact bounce from bare wires testing.
  static bool lastRawBtn1State = HIGH; 
  static bool stableBtn1State = HIGH;
  bool currentBtn1State = digitalRead(BUTTON_PIN_1);

  // If the raw state has changed, reset the debounce timer
  if (currentBtn1State != lastRawBtn1State) {
    lastDebounceTime1 = millis();
  }
  lastRawBtn1State = currentBtn1State;

  // If the state has been stable longer than the debounce time
  if ((millis() - lastDebounceTime1) > BTN1_DEBOUNCE_MS) {
    if (currentBtn1State != stableBtn1State) {
      stableBtn1State = currentBtn1State;
      // Trigger action ONLY on the falling edge (HIGH to LOW)
      if (stableBtn1State == LOW) {
        ui_btn1_click();
      }
    }
  }

  // Dispatch Actions for BTN2 (still on ISR for now)
  if (btn2ClickFlag) {
    btn2ClickFlag = false;
    ui_btn2_click();
  }
}

// -------------------- SETUP --------------------
// Inicializa hardware, configuración persistente y servicios base del firmware.
// Define estado de arranque seguro y prepara módem/GNSS/SD/UI para operación.
void setup() {
  delay(300);
  Serial.begin(115200);
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  // pinMode(POWER_PIN, OUTPUT);//esto no se usa ahora
  // digitalWrite(POWER_PIN, HIGH);//para ver con mini madre

  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 50, 100)); // Blue startup
  pixels.show();

  Serial.println("\n[BOOT] FirmwarePro " + VERSION);
  checkRebootReason();

  prefs.begin("system", false);
  sendCounter = prefs.getUInt("sendCnt", 0);
  sdSaveCounter = prefs.getUInt("sdCnt", 0);
  csvFileName = prefs.getString("csvFile", "");
  wasStreamingBeforeBoot = prefs.getBool("streaming", false);
  prefs.end();
  httpConsecutiveFailCount = 0;
  httpBackoffActive = false;
  httpBackoffUntilMs = 0;

  loadConfig();
  if (!config.sdAutoMount || !config.autoDebug) {
    config.sdAutoMount = true;
    config.autoDebug = true;
    saveConfig();
    Serial.println("[CONFIG] Station defaults enforced: SD auto-mount and Auto Debug ON");
  }
  applyLEDConfig();

  // Create Log Paths
  logFilePath = String("/errors_h") + String(DEVICE_ID_STR) + String(".csv");
  failedTxPath = String("/failed_h") + String(DEVICE_ID_STR) + String(".csv");

  // SSID
  AP_SSID_STR = "HIRIPRO_" + String(DEVICE_ID_STR);

  // Display Init
  u8g2.begin();
  u8g2.setDisplayRotation(config.rotateDisplay ? U8G2_R0 : U8G2_R2);
  u8g2.setFont(u8g2_font_5x7_tf);
  lastOledActivity = millis();
  Wire.begin();
  Wire.setTimeOut(50);
  Wire.setClock(100000);

  // Animation
  while (logoXOffset < LOGO_FINAL_X || hiriXOffset > HIRI_FINAL_X ||
         proYOffset > PRO_FINAL_Y) {
    if (logoXOffset < LOGO_FINAL_X)
      logoXOffset += 4;
    if (hiriXOffset > HIRI_FINAL_X)
      hiriXOffset -= 4;
    if (proYOffset > PRO_FINAL_Y)
      proYOffset -= 1;
    drawAnimation();
    delay(20);
  }
 
  // Show Version
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(58, 9, VERSION.c_str());
  u8g2.setCursor(0, 55);
  u8g2.print("ID:" + String(DEVICE_ID_STR));
  u8g2.sendBuffer();
 delay(2000);
  // PMS & SDS198
  pms.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, Serial2RX_PIN,Serial2TX_PIN); // SDS198 en este caso pero tambien hay otros
                                // sensores pueden usar serial2 Serial2TX_PIN

  // RTC
  if (!rtc.begin()) {
    Serial.println("[RTC] FAIL");
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:FAIL");
  } else {
    rtcOK = true;
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:OK");
  }
  u8g2.sendBuffer();

  // SD Auto Mount early in boot so the startup screen reports SD status.
  spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  SDOK = SD.begin(SD_CS, spiSD);
  if (SDOK) {
    oledStatus("SD OK", "Card mounted");
    delay(700);
    if (!csvFileName.length() || !SD.exists(csvFileName.c_str())) {
      csvFileName = generateCSVFileName();
      writeCSVHeader();
    }
    writeErrorLogHeader();

    prefs.begin("system", false);
    prefs.putString("csvFile", csvFileName);
    prefs.end();
    Serial.println("[BOOT] SD detected");
  } else {
    oledStatus("SD FAIL", "Card not mounted", "Check SD");
    delay(1200);
    Serial.println("[BOOT][SD][ERR] SD auto-mount failed");
  }

  // SDS198 Check (Basic Serial2 verify) legacy
  // Nota: SDS198 no tiene begin() que devuelva bool, asumimos OK si el ID es "06" 
  // o si detectamos tramas mas adelante. Por ahora lo activamos por ID o multisensor.
  if (String(DEVICE_ID_STR) == "06" || String(DEVICE_ID_STR) == "01M") {
    SDS198OK = true;
    Serial.println("[SDS198] Active by ID");
  }

  initI2CSensors();
  if (SHT4xOK) {
    Serial.println("[SHT4X] OK");
    Serial.print("[SHT4X] Serial 0x");
    Serial.println(sht4.readSerial(), HEX);
  }
  if (SHT31OK) {
    Serial.println("[SHT31] OK");
    u8g2.print(" SHT31:OK");
  }
  if (!gas.begin()) {
    logError("I2C_SENSOR_FAIL", "gas.begin", "Gas sensor not found");
  } else {
    Serial.println("[GAS] OK");

    u8g2.print(" GAS:OK");
    Serial.println("The device is connected successfully!");

    // Mode of obtaining data: the main controller needs to request the sensor
    // for data
    gas.changeAcquireMode(gas.PASSIVITY);
    gas.setTempCompensation(gas.ON);
    GasOK = true;
  }
  if (ENS160OK) {
    Serial.println("ENS OK");
    u8g2.print(" ENS160:OK");
    Serial.println("The device is connected successfully!");
  }
  u8g2.sendBuffer();
  delay(1000);

  // BUTTONS (Interrupts)
  pinMode(BUTTON_PIN_1, INPUT);//INPUT_PULLUP #define BUTTON_PIN_1 39 //
  // Button 1 now uses Software Polling to survive bare wire bouncing
  if (BUTTON_PIN_2 >= 0) {
    pinMode(BUTTON_PIN_2, INPUT_PULLUP); // btn 0
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_2), isr_btn2,
                    RISING); // boton enter
  }

  // MODEM
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  modemHardPowerOffSequence();

  oledStatus("MODEM", "Starting...");
  if (!initModemWithRecovery()) {
    oledStatus("MODEM", "FATAL", "ESP RESTART");
    delay(1200);
    ESP.restart();
  }
  pixels.setPixelColor(0, pixels.Color(0, 50, 100));
  pixels.show();
  oledStatus("MODEM", "OK");

  // XTRA
  if (config.gnssEnabled) {
    xtraSupported = detectAndEnableXtra();
    if (xtraSupported) {
      oledStatus("XTRA", "Downloading...");
      xtraLastOk = downloadXtraOnce();
      lastXtraDownload = millis();
    }
  } else {
    xtraSupported = false;
    xtraLastOk = false;
  }

  // GNSS
  gnssBringUp();

  // SD Auto Mount
  // Política actual:
  // - Verificar SD al inicio.
  // - Si antes estaba activo y el reinicio fue "solo" (no SW manual),
  //   reanudar streaming+logging.
  if (SDOK && wasStreamingBeforeBoot) {
    bool rebootWasUnexpected =
        (rebootReason == "Panic" || rebootReason == "IntWatchdog" ||
         rebootReason == "TaskWatchdog" || rebootReason == "OtherWatchdog");

    if (rebootWasUnexpected) {
      streaming = true;
      loggingEnabled = true;
      writeErrorLogHeader();
      Serial.println(
          "[BOOT] Auto-resume enabled (previous state + unexpected reboot)");
    }
  }

  if (config.autostart) {
    streaming = true;
    loggingEnabled = true;
    Serial.println("[BOOT] Autostart enabled: streaming and logging ON");
  }

  uiFullMode = true;
  debugScreenIndex = 0;
  showMessage("MODO DEBUG");
  Serial.println("[BOOT] Debug mode forced ON");

  Serial.println("[READY] Loop starting");
}

// -------------------- LOOP --------------------
bool FirstLoop = true;
// Bucle principal no bloqueante: sensores, UI, watchdog y scheduler de tareas.
// Ejecuta guardado SD y transmisión HTTP en timers separados por configuración.
void loop() {
  esp_task_wdt_reset();

  // Button flags
  // Button Logic (State Check & Dispatch)
  handleButtonLogic();

  if (wifiModeActive) {
    // Modo WiFi Exclusivo:
    // 1. Procesa DNS (Portal Cautivo)
    // 2. Procesa WebServer
    // 3. Mantiene refresco mínimo de pantalla (para no congelar UI)
    // 4. Mantiene lectura mínima de GPS si hay FIX (para no perderlo/saturar
    // buffer), pero sin logica pesada.

    dnsServer.processNextRequest();
    server.handleClient();

    // Mantener GPS vivo (vaciar buffer) si ya teníamos FIX, para no perderlo al
    // salir. No procesamos la data completa para ahorrar CPU, solo lectura
    // básica si es necesario o dejamos que el buffer maneje lo suyo. En este
    // caso, simplemente NO lo apagamos. El módulo sigue encendido. Si queremos
    // mantener el buffer limpio:
    if (config.gnssEnabled && haveFix) {
      // Opcional: leer y descartar o procesar mínimo.
      // Por ahora, confiamos en que el módulo sigue con energía.
      // Solo llamamos al watchdog del GNSS para que no crea que se colgó si
      // implementamos timeout.
      gnssWatchdog();
    }

    static uint32_t lastWifiDisp = 0;
    if (millis() - lastWifiDisp > 500) {
      lastWifiDisp = millis();
      renderDisplay();
    }
    return;
  }

  // Sensors & GNSS
  if (config.gnssEnabled) {
    gnssWatchdog();
    gnssDiagTick();
    gnssDebugPollAsync();
  }

  // Sensors refresh and print data every 2 seconds
  static uint32_t lastSensorUpdateMs = 0;
  if (millis() - lastSensorUpdateMs >= 2000) {
    lastSensorUpdateMs = millis();
    
    // ------------------- RTC Temperature
    if (rtcOK) {
      rtcTempC = rtc.getTemperature();
    }

    Serial.print("PM100: ");
    Serial.print(SDS198PM100);
    Serial.println(" ug/m3");

    // ------------------- Gas Sensor
    Serial.print("Ambient ");
    Serial.print(gas.queryGasType());
    Serial.print(" concentration is: ");
    Serial.print(gas.readGasConcentrationPPM());
    Serial.println(" %vol");
    Serial.println();

    // ------------------- SHT4x
    if (SHT4xOK) {
      sensors_event_t humiditySHT4x, tempSHT4x;
      if (sht4.getEvent(&humiditySHT4x, &tempSHT4x)) {
        tempsht4x = tempSHT4x.temperature;
        humsht4x = humiditySHT4x.relative_humidity;
        sht4xFailCount = 0;
        Serial.print("SHT4x Temperature: "); Serial.print(tempsht4x); Serial.println(" degrees C");
        Serial.print("SHT4x Humidity: ");    Serial.print(humsht4x); Serial.println("% rH");
      } else {
        Serial.println("SHT4x Read FAIL");
        logError("I2C_READ_FAIL", "SHT4X.getEvent", "SHT4x read failed");
        if (++sht4xFailCount >= SHT4X_FAIL_REINIT_THRESHOLD) {
          sht4xFailCount = 0;
          recoverI2CBus("SHT4x repeated read failures");
        }
      }
    }

    // ------------------- ENS160 (Ambient)
    updateEns160State();
    Serial.print("ENS160 status: "); Serial.println(ens160StatusRaw);
    Serial.print("AQI: "); Serial.println(ens160Aqi);
    Serial.print("TVOC: "); Serial.print(ens160Tvoc); Serial.println(" ppb");
    Serial.print("eCO2: "); Serial.print(ens160Eco2); Serial.println(" ppm");
  }

  // First Loop Logic
  if (FirstLoop) {
    csq = modem.getSignalQuality();
    updateNetworkInfo();
    FirstLoop = false;
  }

  // PMS & SDS
  readPMS();
  // Mantener RGB actualizado con PM2.5 aun cuando no haya transmisión HTTP.
  static uint32_t lastLedUpdateMs = 0;
  if (millis() - lastLedUpdateMs >= 300) {
    lastLedUpdateMs = millis();
    updatePmLed((float)PM25);
  }
  byte frame[10];
  // Si se lee una trama válida...bool ();
  if (readFrameSDS198(frame)) {
    // Extrae el valor de PM100 de la trama.
    // El valor de PM100 se forma con los bytes 5 (MSB) y 4 (LSB) de la trama.
    uint16_t pm100 = (uint16_t)((frame[5] << 8) | frame[4]); // en μg/m3
    // Imprime el valor de PM100 en el monitor serie.
    SDS198PM100 = pm100;
    // Serial.print("PM100 (TSP): ");
    // Serial.print(SDS198PM100);
    // Serial.println(" ug/m3");
  }
  // AT Tick
  static uint32_t lastAtTick = 0;
  if (millis() - lastAtTick >= 50) {
    lastAtTick = millis();
    bool d, o;
    (void)atTick(d, o);
  }

  // Battery Sampling
  if (millis() - lastBatSample >= BAT_SAMPLE_INTERVAL_MS) {
    lastBatSample = millis();
    batSampleSum += analogRead(BAT_PIN);
    batSampleCount++;
    if (batSampleCount >= NUM_SAMPLES) {
      float rawV = (batSampleSum / NUM_SAMPLES / 4095.0f) * 3.3f * 2.0f * 1.15f;
      if (batteryVoltageAverage == 0)
        batteryVoltageAverage = rawV;
      batteryVoltageAverage =
          (alpha * rawV) + ((1.0 - alpha) * batteryVoltageAverage);
      batV = batteryVoltageAverage;
      batSampleSum = 0;
      batSampleCount = 0;
    }
  }

  // Display Update
  static uint32_t lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 60) {
    lastDisplayUpdate = millis();
    renderDisplay();
  }

  networkRestartWatchdogTick();
  httpBackoffTick();
  refreshHealthLog();

  // Auto Off
  if (config.oledAutoOff && (millis() - lastOledActivity > config.oledTimeout)) {
    u8g2.setPowerSave(1);
  }

  // Guardado SD (separado de transmisión HTTP)
  // Nota: por diseño de esta etapa, loggingEnabled inicia en false.
  if (loggingEnabled && (millis() - lastSdSave >= config.sdSavePeriod)) {
    lastSdSave = millis();
    bool sdSaved = saveCSVData();
    lastSdActivityMs = millis();
    lastSdOk = sdSaved;
    // Sin pantalla emergente de "guardado": se usan indicadores del header/RGB.
  }

  // Transmisión HTTP (separada de guardado SD)
  if (streaming && !httpBackoffActive &&
      (millis() - lastHttpSend >= config.httpSendPeriod)) {
    lastHttpSend = millis();
    bool networkTxReady = modem.isNetworkConnected() &&
                          (registrationStatus == "Registered") &&
                          (csq != 99);
    bool txOk = false;
    if (networkTxReady) {
      txOk = sendCurrentMeasurement();
    } else {
      hasRed = false;
      handleHttpFailureState("network_not_ready");
      logError("HTTP_SKIP_NO_NETWORK", "loop",
               "Skipping HTTP send because network/SIM is not ready");
      Serial.println("[HTTP] SKIP no network/SIM ready");
    }
    hasHttpAttempted = true;
    lastHttpActivityMs = millis();
    lastHttpOk = txOk;
  }

  // Serial Commands
  processSerialCommand();

  if (bootHttpAttemptPending) {
    bootHttpAttemptPending = false;
    if (streaming && !httpBackoffActive) {
      bool networkTxReady = modem.isNetworkConnected() &&
                            (registrationStatus == "Registered") &&
                            (csq != 99);
      bool txOk = false;
      if (networkTxReady) {
        txOk = sendCurrentMeasurement();
      } else {
        hasRed = false;
        handleHttpFailureState("boot_network_not_ready");
        logError("HTTP_SKIP_NO_NETWORK", "boot_loop",
                 "Skipping boot HTTP send because network/SIM is not ready");
        Serial.println("[HTTP][BOOT] SKIP no network/SIM ready");
      }
      hasHttpAttempted = true;
      lastHttpActivityMs = millis();
      lastHttpOk = txOk;
      lastHttpSend = millis();
    }
  }
}
