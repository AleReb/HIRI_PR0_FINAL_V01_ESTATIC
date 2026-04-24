

// -------------------- Safe value helpers --------------------
static inline String missingUrlValue() { return "-1"; }

static inline String safeFloatStr(float v) {
  if (isnan(v) || isinf(v))
    return missingUrlValue();
  return String(v, 3);
}

static inline String safeUIntStr(uint32_t v) { return String(v); }

static inline String safeIntStr(int v) { return String(v); }

static inline String safeGpsStr(const String &s) {
  if (s.length() == 0)
    return missingUrlValue();
  if (s == "NaN" || s == "N/A")
    return missingUrlValue();
  return s;
}

static inline String safeSatsStr(const String &s) {
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
