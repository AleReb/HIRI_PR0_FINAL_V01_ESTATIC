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
