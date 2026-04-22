/*
 * FirmwarePro.ino
 * Merged Firmware: GPSDebug Backend + Menu UI
 *
 * Mapeo de Sensores por Dispositivo (Refactor V0.2):
 * | DEVICE_ID_STR | BaseID | Descripcion
 * | :---          | :---   | :---
 * | "1"           | 401    | Sensor 1
 * | "2"           | 406    | Sensor 2
 * | "3"           | 415    | Sensor 3 (con SHT31)
 * | "4"           | 448    | Sensor 4
 * | "5"           | 454    | Sensor 5
 * | "6"           | 460    | Sensor 6 (con SDS198)
 * | "7"           | 468    | Sensor 7
 * | "8"           | 473    | Sensor 8
 * | "9"           | 478    | Sensor 9
 * | "10"          | 484    | Sensor 10
 * | "80"          | 927    | Sensor 80
 * | "81"          | 933    | Sensor 81
 * | "82"          | 939    | Sensor 82
 * | "01M"         | 10xx   | Multi-Sensor Especial
 *
 * Offsets de Variables (Base + X):
 * +0: PMS (Var 3,6,7,8,9)
 * +1: Modem (Var 11,12,15,45,46)
 * +2: RTC (Var 3)
 * +3: Bat (Var 4)
 * +4: Sys (Var 11,12,42,43,44)
 * +5: SHT (Var 3,6)
 * +7: SDS198 (Var 51)
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
String VERSION = "Pro V0.1.1E";

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
String currentNote = "3"; // Global note for one-shot logging
// Variables moved to main for centralization
String lastSavedCSVLine = ""; // Used in sd_card.ino for OLED display
File uploadFile;              // Used in wifi.ino for file uploads

String deviceID = "/HIRIP";
const char *DEVICE_ID_STR = "01M"; // ID del dispositivo actual "01M" es el modelo estatico
String AP_SSID_STR = "";
const char *AP_PASSWORD = "12345678";
String apIpStr = "0.0.0.0";
//////////
//vamos a mover las urls a helpers
//https://api-sensores.cmasccp.cl/insertarMedicion?idsSensores=1009,1010,1010,1011,1011,1011,1011,1011,1012,1013,1013,1013,1013,1013,1014,1015,1015&idsVariables=53,54,55,11,12,15,45,46,4,3,6,7,8,9,51,3,6&valores=
//Dioxido de Azufre (So2,ppm) ppm(1009),TVOC ppb(1010),eCO2 ppm(1010),Latitud °(1011),Longitud °(1011),Intensidad señal telefónica Adimensional(1011),Velocidad_km/h km/h(1011),Satelites int(1011),Voltaje V(1012),Grados celcius °C(1013),Humedad %(1013),Material particulado PM 1.0 µg/m³(1013),Material particulado PM 2.5 µg/m³(1013),Material particulado PM 10 µg/m³(1013),Material particulado PM 100 µg/m³(1014),Grados celcius °C(1015),Humedad %(1015)
// -------------------- Measurements API (real endpoint) --------------------
const char *API_BASE = "http://api-sensores.cmasccp.cl/insertarMedicion";
// Must match backend exactly:
//const char *IDS_SENSORES = "401,401,401,401,401,402,402,402,402,402,403,404,405,405,405,405,405"; // sensor 1 
//const char* IDS_SENSORES ="406,406,406,406,406,407,407,407,407,407,408,409,410,410,410,410,410"; //sensor 2 
//const char* IDS_SENSORES = "415,415,415,415,415,416,416,416,416,416,417,418,419,419,419,419,419,420,420"; //sensor 3 //tiene sht31 
//const char* IDS_SENSORES = "448,448,448,448,448,449,449,449,449,449,450,451,452,452,452,452,452,453,453"; //sensor 4 //cuatro no actualizado en dictuc 
//const char* IDS_SENSORES = "454,454,454,454,454,455,455,455,455,455,456,457,458,458,458,458,458,459,459"; //sensor 5
//const char *IDS_SENSORES ="460,460,460,460,460,461,461,461,461,461,462,463,464,464,464,464,464,467"; // sensor 6   // tiene un sensor SDS198
//const char* IDS_SENSORES = "468,468,468,468,468,469,469,469,469,469,470,471,472,472,472,472,472"; //sensor 7 
//const char* IDS_SENSORES ="473,473,473,473,473,474,474,474,474,474,475,476,477,477,477,477,477"; //sensor 8 
//const char* IDS_SENSORES = "478,478,478,478,478,479,479,479,479,479,480,481,482,482,482,482,482,483,483"; //sensor 9 
//const char* IDS_SENSORES = "484,484,484,484,484,485,485,485,485,485,486,487,488,488,488,488,488,489,489"; //sensor 10 
//const char* IDS_SENSORES = "927,927,927,927,927,928,928,928,928,928,929,930,931,931,931,931,931,932,932"; //sensor 80 
//const char* IDS_SENSORES = "933,933,933,933,933,934,934,934,934,934,935,936,937,937,937,937,937,938,938";//sensor 81 
//const char* IDS_SENSORES = "939,939,939,939,939,940,940,940,940,940,941,942,943,943,943,943,943,944,944"; //sensor 82

const char *IDS_VARIABLES = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44"; // los mismos datos pero
                                                   // cambia el ID-sensor cambia
                                                   // el numero de sensores
const char *IDS_VARIABLESSHT31 = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,3,6"; // los mismos datos pero
                                                       // caria el ID-sensor
                                                       // cambia el numero de
                                                       // sensores
const char *IDS_VARIABLES06 = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,51"; // los mismos datos pero
                                                      // caria el ID-sensor
                                                      // cambia el numero de
                                                      // sensores
                                                      //  ur format helpers
String valores;
String url;

// APN
const char apn[] = "gigsky-02";
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
void handleButtonLogic(); // Renamed from dispatchButtonFlags
void showMessage(const char *msg); // From ui.ino
extern bool uiFullMode; // From ui.ino

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

// -------------------- Telemetry Tx --------------------
// NOTA DE INTEGRACION:
// - "streaming" controla transmisión HTTP.
// - "loggingEnabled" controla guardado en SD.
// - Ambos están separados a propósito para evitar acoplar guardar/transmitir.
// Construye payload/URL de medición según hardware activo y envía por HTTP.
// Persiste contadores en flash y registra fallos en SD cuando corresponde.
extern bool SHT31OK, SHT4xOK, SDS198OK, GasOK, ENS160OK;
extern String currentNote;
extern int SDS198PM100;
extern float tempsht31, humsht31, tempsht4x, humsht4x;
extern DFRobot_GAS_I2C gas;
extern DFRobot_ENS160_I2C ENS160;

// Helper para añadir bloques a la URL (idsSensores, idsVariables, valores)
void addBlock(String &idsS, String &idsV, String &vals, const String &sId, const String &vId, const String &val) {
  if (idsS.length() > 0) {
    idsS += ",";
    idsV += ",";
    vals += ",";
  }
  idsS += sId;
  idsV += vId;
  vals += val;
}

bool sendCurrentMeasurement() {
  String idsSensores = "";
  String idsVariables = "";
  String valores = "";

  String devIdStr = String(DEVICE_ID_STR);
  int baseId = getBaseSensorId(devIdStr);
  bool isMulti = (devIdStr == "01M");// esto deberia actualizarse segun el hardware que se conecte

  // --- BLOQUE ESPECIAL GAS (01M o GasOK)
  if (isMulti || GasOK) {
    String sId = isMulti ? "1009" : String(baseId); 
    addBlock(idsSensores, idsVariables, valores, sId, "53", safeFloatStr(gas.readGasConcentrationPPM()));
  }

  // --- BLOQUE ESPECIAL ENS160 (01M o ENSOK)
  if (isMulti || ENS160OK) {
    String sId = isMulti ? "1010" : String(baseId); 
    addBlock(idsSensores, idsVariables, valores, sId, "54", safeIntStr(ENS160.getTVOC()));
    addBlock(idsSensores, idsVariables, valores, sId, "55", safeIntStr(ENS160.getECO2()));
  }

  // --- BLOQUE PMS (Base + 0 o 1013)
  String sIdPms = isMulti ? "1013" : String(baseId);
  addBlock(idsSensores, idsVariables, valores, sIdPms, "3", isnan(pmsTempC) ? "0" : safeFloatStr(pmsTempC));
  addBlock(idsSensores, idsVariables, valores, sIdPms, "6", isnan(pmsHum) ? "0" : safeFloatStr(pmsHum));
  addBlock(idsSensores, idsVariables, valores, sIdPms, "7", safeUIntStr(PM1));
  addBlock(idsSensores, idsVariables, valores, sIdPms, "8", safeUIntStr(PM25));
  addBlock(idsSensores, idsVariables, valores, sIdPms, "9", safeUIntStr(PM10));

  // --- BLOQUE GPS/MODEM (Base + 1 o 1011)
  String sIdGps = isMulti ? "1011" : String(baseId + 1);
  addBlock(idsSensores, idsVariables, valores, sIdGps, "11", safeGpsStr(gpsLat));
  addBlock(idsSensores, idsVariables, valores, sIdGps, "12", safeGpsStr(gpsLon));
  addBlock(idsSensores, idsVariables, valores, sIdGps, "15", safeIntStr(csq));
  addBlock(idsSensores, idsVariables, valores, sIdGps, "45", (gpsSpeedKmh.length() ? gpsSpeedKmh : "0"));
  addBlock(idsSensores, idsVariables, valores, sIdGps, "46", safeSatsStr(satellitesStr));

  // --- BLOQUE BATERIA (Base + 3 o 1012)
  String sIdBat = isMulti ? "1012" : String(baseId + 3);
  addBlock(idsSensores, idsVariables, valores, sIdBat, "4", safeFloatStr(batV));

  // --- BLOQUE RTC (Base + 2 o 1013)
  if (rtcOK) {
    String sIdRtc = isMulti ? "1013" : String(baseId + 2);
    addBlock(idsSensores, idsVariables, valores, sIdRtc, "3", safeFloatStr(rtcTempC));
  }

  // --- BLOQUE SISTEMA/LOG (Base + 4) -> No usado en 01M segun string propuesto
  if (!isMulti && baseId != -1) {
    String sIdSys = String(baseId + 4);
    addBlock(idsSensores, idsVariables, valores, sIdSys, "11", safeGpsStr(gpsLat));
    addBlock(idsSensores, idsVariables, valores, sIdSys, "12", safeGpsStr(gpsLon));
    addBlock(idsSensores, idsVariables, valores, sIdSys, "42", safeUIntStr(sendCounter + 1));
    addBlock(idsSensores, idsVariables, valores, sIdSys, "43", loggingEnabled ? "1" : "0");
    addBlock(idsSensores, idsVariables, valores, sIdSys, "44", currentNote.length() ? currentNote : "0");
  }

  // --- BLOQUE SDS198 (Base + 7 o 1014)
  if (SDS198OK) {
    String sIdSds = isMulti ? "1014" : String(baseId + 7);
    addBlock(idsSensores, idsVariables, valores, sIdSds, "51", safeUIntStr(SDS198PM100));
  }

  // --- BLOQUE SHT (Base + 5 o 1015)
  if (SHT31OK || SHT4xOK) {
    String sIdSht = isMulti ? "1015" : String(baseId + 5);
    // Priorizamos SHT4x si ambos están presentes (debido a posible colisión de dirección I2C 0x44)
    float t = SHT4xOK ? tempsht4x : tempsht31;
    float h = SHT4xOK ? humsht4x : humsht31;
    addBlock(idsSensores, idsVariables, valores, sIdSht, "3", safeFloatStr(t));
    addBlock(idsSensores, idsVariables, valores, sIdSht, "6", safeFloatStr(h));
  }

  String fullUrl = String(API_BASE) + "?idsSensores=" + idsSensores + 
                   "&idsVariables=" + idsVariables + "&valores=" + valores;

  Serial.println("[HTTP] GET " + fullUrl);
  if (httpGet_webhook(fullUrl)) {
    sendCounter++;
    prefs.begin("system", false);
    prefs.putUInt("sendCnt", sendCounter);
    prefs.end();
    Serial.println("[HTTP] OK");
    return true;
  }

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

  // Estado runString apIpStr = "0.0.0.0";
volatile bool streaming = false;
volatile bool loggingEnabled = false;

// UI Rotation (Modo Debug)
uint8_t debugScreenIndex = 0;
uint32_t lastDebugRotationMs = 0;
const uint32_t DEBUG_ROTATION_INTERVAL_MS = 10000;

  loadConfig();
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

  // SDS198 Check (Basic Serial2 verify)
  // Nota: SDS198 no tiene begin() que devuelva bool, asumimos OK si el ID es "06" 
  // o si detectamos tramas mas adelante. Por ahora lo activamos por ID o multisensor.
  if (String(DEVICE_ID_STR) == "06" || String(DEVICE_ID_STR) == "01M") {
    SDS198OK = true;
    Serial.println("[SDS198] Active by ID");
  }

  Serial.println("Adafruit SHT4x test");
  if (! sht4.begin()) {
    Serial.println("Couldn't find SHT4x");
    SHT4xOK = false;
  }else{
  Serial.println("Found SHT4x sensor");
  Serial.print("Serial number 0x");
  Serial.println(sht4.readSerial(), HEX);

  // You can have 3 different precisions, higher precision takes longer
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  switch (sht4.getPrecision()) {
     case SHT4X_HIGH_PRECISION: 
       Serial.println("High precision");
       break;
     case SHT4X_MED_PRECISION: 
       Serial.println("Med precision");
       break;
     case SHT4X_LOW_PRECISION: 
       Serial.println("Low precision");
       break;
  }
  // You can have 6 different heater settings
  // higher heat and longer times uses more power
  // and reads will take longer too!
  sht4.setHeater(SHT4X_NO_HEATER);
  // switch (sht4.getHeater()) {
  //    case SHT4X_NO_HEATER: 
  //      Serial.println("No heater");
  //      break;
  //    case SHT4X_HIGH_HEATER_1S: 
  //      Serial.println("High heat for 1 second");
  //      break;
  //    case SHT4X_HIGH_HEATER_100MS: 
  //      Serial.println("High heat for 0.1 second");
  //      break;
  //    case SHT4X_MED_HEATER_1S: 
  //      Serial.println("Medium heat for 1 second");
  //      break;
  //    case SHT4X_MED_HEATER_100MS: 
  //      Serial.println("Medium heat for 0.1 second");
  //      break;
  //    case SHT4X_LOW_HEATER_1S: 
  //      Serial.println("Low heat for 1 second");
  //      break;
  //    case SHT4X_LOW_HEATER_100MS: 
  //      Serial.println("Low heat for 0.1 second");
  //      break;
  // }
  SHT4xOK = true;
  }
  // SHT31
  if (!sht31.begin(0x44)) {
    Serial.println("[SHT31] FAIL");
    SHT31OK = false;
  } else {
    Serial.println("[SHT31] OK");
    SHT31OK = true;
    u8g2.print(" SHT31:OK");
  }
  if (!gas.begin()) {
    Serial.println("NO Deivces !");
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
  if (NO_ERR != ENS160.begin()) {
    Serial.println("Communication with device failed, please check connection");
  } else {
    Serial.println("ENS OK");

    u8g2.print(" ENS160:OK");
    Serial.println("The device is connected successfully!");
    /**
     * Set power mode
     * mode Configurable power mode:
     *   ENS160_SLEEP_MODE: DEEP SLEEP mode (low power standby)
     *   ENS160_IDLE_MODE: IDLE mode (low-power)
     *   ENS160_STANDARD_MODE: STANDARD Gas Sensing Modes
     */
    ENS160.setPWRMode(ENS160_STANDARD_MODE);
    ENS160OK = true;
  }
  u8g2.sendBuffer();
  delay(1000);

  // BUTTONS (Interrupts)
  pinMode(BUTTON_PIN_1, INPUT);//INPUT_PULLUP #define BUTTON_PIN_1 39 //
  pinMode(BUTTON_PIN_2, INPUT_PULLUP); //btn 0
  // Button 1 now uses Software Polling to survive bare wire bouncing
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_2), isr_btn2, RISING);//boton enter

  // MODEM
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(300);
  digitalWrite(MODEM_PWRKEY, LOW);
  pinMode(MODEM_FLIGHT, OUTPUT);
  digitalWrite(MODEM_FLIGHT, HIGH);
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);

  oledStatus("MODEM", "Starting...");

  // LED heartbeat during modem startup (visual anti-freeze feedback)
  bool modemBlinkState = false;
  int dot = 1;
  for (int i = 0; i < 3; i++) {

    dot++;
    while (!modem.testAT(1000)) {
      Serial.println("[MODEM] Retry...");
      oledStatus("MODEM", "Retry: ", String(dot));
      // Blink RGB while retrying modem init
      modemBlinkState = !modemBlinkState;
      if (modemBlinkState) {
        pixels.setPixelColor(0, pixels.Color(0, 0, 80)); // soft blue
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
      pixels.show();

      digitalWrite(MODEM_PWRKEY, HIGH);
      delay(300);
      digitalWrite(MODEM_PWRKEY, LOW);
      delay(1000);
    }
  }

  // Solid blue when modem is ready
  pixels.setPixelColor(0, pixels.Color(0, 50, 100));
  pixels.show();

  oledStatus("MODEM", "OK");

  // Modem setup
  atRun("+CEDRXS=0", "OK", "ERROR", 1500);
  atRun("+CPSMS=0", "OK", "ERROR", 1500);

  // Network
  oledStatus("NET", "Attach/PDP...");
  if (!modem.waitForNetwork(60000))
    oledStatus("NET", "Attach FAIL");
  else {
    if (!modem.gprsConnect(apn, gprsUser, gprsPass))
      oledStatus("NET", "PDP FAIL");
    else
      oledStatus("NET", "PDP OK");
  }

  // XTRA
  xtraSupported = detectAndEnableXtra();
  if (xtraSupported) {
    oledStatus("XTRA", "Downloading...");
    xtraLastOk = downloadXtraOnce();
    lastXtraDownload = millis();
  }

  // GNSS
  gnssBringUp();

  // Watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  // SD Auto Mount
  // Política actual:
  // - Verificar SD al inicio.
  // - Si antes estaba activo y el reinicio fue "solo" (no SW manual),
  //   reanudar streaming+logging.
  if (config.sdAutoMount || wasStreamingBeforeBoot) {
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);
    if (SDOK) {
      if (!csvFileName.length() || !SD.exists(csvFileName.c_str())) {
        csvFileName = generateCSVFileName();
        writeCSVHeader();
      }

      prefs.begin("system", false);
      prefs.putString("csvFile", csvFileName);
      prefs.end();

      // Autoresume SOLO en reinicios claramente inesperados por watchdog/panic.
      // Evita retomar streaming tras reinicios manuales, power-on o estados
      // ambiguos.
      bool rebootWasUnexpected =
          (rebootReason == "Panic" || rebootReason == "IntWatchdog" ||
           rebootReason == "TaskWatchdog" || rebootReason == "OtherWatchdog");

      if (wasStreamingBeforeBoot && rebootWasUnexpected) {
        streaming = true;
        loggingEnabled = true;
        writeErrorLogHeader();
        Serial.println(
            "[BOOT] Auto-resume enabled (previous state + unexpected reboot)");
      } else {
        Serial.println("[BOOT] SD detected. Streaming/logging remain OFF");
      }
    }
  }

  if (config.autostart) {
    streaming = true;
    loggingEnabled = true;
    Serial.println("[BOOT] Autostart enabled: streaming and logging ON");
  }

  if (config.autoDebug) {
    uiFullMode = true;
    debugScreenIndex = 0;
    showMessage("MODO DEBUG");
    Serial.println("[BOOT] Auto Debug enabled");
  }

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
    if (haveFix) {
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
  gnssWatchdog();
  gnssDiagTick();
  gnssDebugPollAsync();

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
        Serial.print("SHT4x Temperature: "); Serial.print(tempsht4x); Serial.println(" degrees C");
        Serial.print("SHT4x Humidity: ");    Serial.print(humsht4x); Serial.println("% rH");
      } else {
        Serial.println("SHT4x Read FAIL");
      }
    }

    // ------------------- ENS160 (Ambient)
    ENS160.setTempAndHum(/*temperature=*/pmsTempC, /*humidity=*/pmsHum);
    uint8_t Status = ENS160.getENS160Status();
    Serial.print("ENS160 status: "); Serial.println(Status);
    Serial.print("AQI: "); Serial.println(ENS160.getAQI());
    Serial.print("TVOC: "); Serial.print(ENS160.getTVOC()); Serial.println(" ppb");
    Serial.print("eCO2: "); Serial.print(ENS160.getECO2()); Serial.println(" ppm");
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

  // Auto Off
  if (config.oledAutoOff &&
      (millis() - lastOledActivity > config.oledTimeout)) {
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
  if (streaming && (millis() - lastHttpSend >= config.httpSendPeriod)) {
    lastHttpSend = millis();
    bool txOk = sendCurrentMeasurement();
    lastHttpActivityMs = millis();
    lastHttpOk = txOk;
  }

  // Serial Commands
  processSerialCommand();
}
