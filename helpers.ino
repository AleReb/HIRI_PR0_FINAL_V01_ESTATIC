

// -------------------- Safe value helpers --------------------
static inline String safeFloatStr(float v) {
  if (isnan(v) || isinf(v))
    return "0";
  return String(v, 3);
}

static inline String safeUIntStr(uint32_t v) { return String(v); }

static inline String safeIntStr(int v) { return String(v); }

// -------------------- Dynamic URL Generation --------------------
int getBaseSensorId(const String &deviceId) {
  if (deviceId == "1") return 401;
  if (deviceId == "2") return 406;
  if (deviceId == "3") return 415;
  if (deviceId == "4") return 448;
  if (deviceId == "5") return 454;
  if (deviceId == "6") return 460;
  if (deviceId == "7") return 468;
  if (deviceId == "8") return 473;
  if (deviceId == "9") return 478;
  if (deviceId == "10") return 484;
  if (deviceId == "80") return 927;
  if (deviceId == "81") return 933;
  if (deviceId == "82") return 939;
  return -1; // Especial o desconocido
}

static inline String safeGpsStr(const String &s) {
  if (s.length() == 0)
    return "0";
  if (s == "NaN" || s == "N/A")
    return "0";
  return s;
}

static inline String safeSatsStr(const String &s) {
  if (s.length() == 0)
    return "0";
  for (size_t i = 0; i < s.length(); ++i) {
    if (!isDigit(s[i]))
      return "0";
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
}

// -------------------- Error Logging --------------------
void logError(const String &type, const String &ctx, const String &msg) {
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
