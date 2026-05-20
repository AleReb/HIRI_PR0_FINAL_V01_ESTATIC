#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Modem definition must be before include
#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 4096

// Modem Pins (SIM7600)
#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define MODEM_DTR 32
#define MODEM_FLIGHT 25

// PMS5003 Pins (SoftwareSerial)
#define pms_RX 18
#define pms_TX 5

// // Battery / Power HIRI PRO
// #define BAT_PIN 35
// #define POWER_PIN 33
// #define NEOPIX_PIN 12
// #define NUMPIXELS 1
// #define BUTTON_PIN_1 19
// #define BUTTON_PIN_2 23

// Battery / Power prueba HIRIDUST/VALPO
#define BAT_PIN 35
#define POWER_PIN 33
#define NEOPIX_PIN 12
#define NUMPIXELS 1
/*
NOTA EN LA PLACA VIEJA COYAHIQUE SOLO ESTA EL BOTON 39
*/
#define BUTTON_PIN_1 39 //este pin necesita resistencia extra lee low al apretarse y high al soltarse
// Boton 2 deshabilitado por defecto.
// esto esta en el void setup, como 
// Para habilitarlo de nuevo, cambia -1 por el GPIO real. Ejemplo: #define BUTTON_PIN_2 0
#define BUTTON_PIN_2 -1

#define Serial2RX_PIN 23 // Pin RX del ESP32 conectado al TX del sensor.
#define Serial2TX_PIN 19  // Pin TX del ESP32 (no se usa para recibir datos del sensor).

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <TinyGsmClient.h>

// -------------------- Configuration System --------------------
struct SystemConfig {
  // SD Card
  bool sdAutoMount;      // Montar SD en boot (default: false)
  uint32_t sdSavePeriod; // Período guardado SD en ms (default: 180000)

  // HTTP Transmission
  uint32_t httpSendPeriod; // Período transmisión en ms (default: 300000)
  uint16_t httpTimeout;    // Timeout HTTP en segundos (default: 15)

  // Display OLED
  bool oledAutoOff;     // Apagar OLED automáticamente (default: false)
  uint32_t oledTimeout; // Timeout en ms (default: 120000 = 2min)

  // Power Management
  bool ledEnabled;       // NeoPixel habilitado (default: true)
  uint8_t ledBrightness; // Brillo LED: 10, 25, 50, 100 (default: 50%)

  // Autostart
  bool autostart; // Iniciar streaming/logging al encender (default: true)
  bool autostartWaitGps; // Esperar GPS fix antes de iniciar (default: false)
  uint16_t  autostartGpsTimeout; // Timeout GPS en segundos (default: 600 = 10min)
  bool autoDebug; // Iniciar automáticamente en modo debug (default: true)

  // System
  bool rotateDisplay; // Rotar display 180° (default: false)
  uint16_t scheduledRebootHours; // Reinicio programado en horas; 0 = deshabilitado

  // GNSS Power
  bool gnssEnabled; // Habilita GNSS completo y su recuperacion automatica

  // GNSS Mode
  uint8_t gnssMode; // Modo GNSS: 1=GPS, 3=GPS+GLO, 5=GPS+BDS, 7=GPS+GLO+BDS,
                    // 15=ALL (default: 15)
};

// -------------------- Display State Machine --------------------
enum DisplayState {
  DISP_NORMAL,
  DISP_SD_SAVED,
  DISP_MESSAGE,
  DISP_PROMPT,
  DISP_NETWORK,
  DISP_RTC,
  DISP_STORAGE,
  DISP_GPS,
  DISP_WIFI
};
#define DISP_MSG_DURATION_MS 1500

#endif
