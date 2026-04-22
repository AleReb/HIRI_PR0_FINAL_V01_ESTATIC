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
extern float tempsht31, humsht31, tempsht4x, humsht4x;
extern String hdopStr, gpsAlt;
extern uint32_t sendCounter, sdSaveCounter;
extern DFRobot_GAS_I2C gas;
extern DFRobot_ENS160_I2C ENS160;
extern int SDS198PM100;
extern uint16_t PM10, PM1;

// -------------------- Helper Logic --------------------

extern String apIpStr;
extern String AP_SSID_STR;
extern const char *AP_PASSWORD;

// --- Helper Logic ---

// Pantalla dedicada de control WiFi (estilo RTC)
void drawWifiModeScreen() {
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

// Helper to show message non-blocking
void showMessage(const char *msg) {
  strncpy(uiMessage, msg, sizeof(uiMessage) - 1);
  uiMessage[sizeof(uiMessage) - 1] = '\0';
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
  if (haveFix && gpsStatus == "Fix") {
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

  // Signal (movido +10 px para evitar solape)
  bool networkError = (csq == 99);
  if (networkError) {
    u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2.drawGlyph(84, 9, 0x0118);
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
    baseF = "Temperatura (C)";
  } else if (idx == 2) { // Hum
    if (isnan(pmsHum))
      snprintf(buf, sizeof(buf), "--.-");
    else
      dtostrf(pmsHum, 0, 1, buf);
    baseF = "Humedad (%)";
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
  
  // Cabecera de la pantalla actual
  u8g2.setCursor(110, 16);
  u8g2.print(String(debugScreenIndex + 1) + "/4");

  if (debugScreenIndex == 0) { // --- SCREEN 0: GPS & TIME ---
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

  } else if (debugScreenIndex == 1) { // --- SCREEN 1: PM (AIR QUALITY) ---
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

  } else if (debugScreenIndex == 2) { // --- SCREEN 2: ENV & GAS ---
    u8g2.drawStr(0, 16, "DEBUG: ENV & GAS");
    int y = 26;
    if (SHT4xOK || SHT31OK) {
      float t = SHT4xOK ? tempsht4x : tempsht31;
      float h = SHT4xOK ? humsht4x : humsht31;
      u8g2.setCursor(0, y);
      u8g2.print(String(SHT4xOK ? "SHT40: " : "SHT31: ") + String(t, 1) + "C / " + String(h, 1) + "%");
      y += 8;
    }
    if (ENS160OK) {
      u8g2.setCursor(0, y);
      u8g2.print("AQI:" + String(ENS160.getAQI()) + " TVOC:" + String(ENS160.getTVOC()) + "ppb");
      y += 8;
      u8g2.setCursor(0, y);
      u8g2.print("eCO2: " + String(ENS160.getECO2()) + " ppm");
      y += 8;
    }
    if (GasOK) {
      u8g2.setCursor(0, y);
      u8g2.print("GAS: " + String(gas.readGasConcentrationPPM(), 2) + " PPM");
    }

  } else if (debugScreenIndex == 3) { // --- SCREEN 3: SYSTEM ---
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
  u8g2.clearBuffer();
  drawHeader();

  // Special States
  if (displayState == DISP_MESSAGE) {
    if (millis() - displayStateStartTime < DISP_MSG_DURATION_MS) {
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
    if (millis() - lastDebugRotationMs >= DEBUG_ROTATION_INTERVAL_MS) {
      lastDebugRotationMs = millis();
      debugScreenIndex = (debugScreenIndex + 1) % 4;
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
    // En modo FULL, BTN1 click ahora INICIA/DETIENE el muestreo (Acción rápida)
    // Reutilizamos la lógica de toggle que antes estaba en BTN2
    toggleSamplingAction();
    renderDisplay();
    return;
  }

  menuIndex = (menuIndex + 1) % menus[menuDepth].count;
  lastOledActivity = millis();
  if (config.oledAutoOff)
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
  if (config.oledAutoOff)
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
