"""Run the firmware's real connection and local scheduler code with a fake modem.

Requires Python and g++; no ESP32 or SIM required. Does not replace board tests.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
http = (ROOT / "http.ino").read_text(encoding="utf-8")
main = (ROOT / "HIRI_PR0_FINAL_V01_ESTATIC.ino").read_text(encoding="utf-8")
connection = http[http.index("static const uint8_t CELLULAR_MAX_ATTEMPTS"):http.index("// Helper: parsear")]
local = main[main.index("void serviceLocalData() {"):main.index("// -------------------- LOOP")]
stubs = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
struct String : std::string {
  using std::string::string;
  String(const std::string &s): std::string(s) {}
  int indexOf(const char *s) const { auto p=find(s); return p==npos ? -1 : int(p); }
};
struct SerialMock {
  void println(const char*) {}
  template<class... T> void printf(const char*, T...) {}
} Serial;
uint32_t clockMs=0;
uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs+=ms; }
void esp_task_wdt_reset() {}
bool modemReady=false, hasRed=false, localAcquisitionReady=true;
bool loggingEnabled=true, SDOK=true, lastSdOk=false, SDS198OK=false;
uint32_t lastSdSave=0,lastSdActivityMs=0,lastBatSample=0;
const uint32_t BAT_SAMPLE_INTERVAL_MS=100;
const int NUM_SAMPLES=10, BAT_PIN=35;
float batSampleSum=0,batV=0;
int batSampleCount=0,PM25=0,SDS198PM100=0;
struct { uint32_t sdSavePeriod=180000; } config;
int samples=0,saves=0,pmsReads=0,commands=0,attempts=0,opens=0;
using byte=uint8_t;
void refreshSensors2s() { ++samples; }
void readPMS() { ++pmsReads; }
void updatePmLed(float) {}
bool readFrameSDS198(byte*) { return false; }
int analogRead(int) { return 1000; }
bool saveCSVData() { if (!SDOK || !loggingEnabled) return false; ++saves; return true; }
const char apn[]="test",gprsUser[]="",gprsPass[]="";
enum Scenario { NO_MODEM, NO_SIM, LOCKED_SIM, NO_NETWORK, PDP_FAIL, SUCCESS, SECOND_TRY } scenario;
bool opened=false;
void serviceLocalData();
void elapse(uint32_t ms) {
  for(uint32_t i=0;i<ms;i+=100) { delay(100); serviceLocalData(); }
}
bool sendAtSync(const String &cmd, String &r, uint32_t timeout) {
  ++commands; r="OK";
  if(cmd.empty()) { ++attempts; if(scenario==NO_MODEM) { elapse(timeout); return false; } }
  if(cmd=="+CPIN?") {
    if(scenario==NO_SIM || (scenario==SECOND_TRY && attempts==1)) { r="ERROR"; return false; }
    r=scenario==LOCKED_SIM ? "+CPIN: SIM PIN\nOK" : "+CPIN: READY\nOK";
  }
  if(cmd=="+CEREG?" || cmd=="+CGREG?") r=scenario==NO_NETWORK ? "+CEREG: 0,2\n+CGREG: 0,2\nOK" : "+CEREG: 0,1\n+CGREG: 0,5\nOK";
  if(cmd=="+NETOPEN?") r=opened ? "+NETOPEN: 1\nOK" : "+NETOPEN: 0\nOK";
  return true;
}
bool atRun(const String &cmd,const String &expect,const String &,uint32_t timeout) {
  assert(cmd=="+NETOPEN" && expect=="+NETOPEN: 0"); ++opens;
  if(scenario==PDP_FAIL) { elapse(timeout); return false; }
  opened=true; return true;
}
'''
cases = r'''
int main(int argc,char**argv) {
  assert(argc==2); scenario=Scenario(std::stoi(argv[1]));
  assert(cellularRegistered("+CEREG: 0,1\nOK", "+CEREG:"));
  assert(cellularRegistered("+CGREG: 0,5\nOK", "+CGREG:"));
  assert(!cellularRegistered("+CEREG: 0,2\nOK", "+CEREG:"));
  assert(!cellularRegistered("ERROR", "+CEREG:"));
  // Start 500ms before a real SD deadline, ensuring waits service storage.
  clockMs=179500;
  bool ok=ensurePdpAndNet();
  if(scenario==SUCCESS || scenario==SECOND_TRY) {
    assert(ok && hasRed && !cellularBackoffActive);
    assert(attempts==(scenario==SUCCESS ? 1 : 2));
    assert(opens==1);
    assert(ensurePdpAndNet()); // Already open; do not reopen the PDP.
    assert(opens==1);
  } else {
    assert(!ok && !hasRed && cellularBackoffActive && attempts==3);
    assert(saves>=1 && samples>0 && pmsReads>0);
    assert(opens==(scenario==PDP_FAIL ? 3 : 0));
    int previousCommands=commands;
    assert(!ensurePdpAndNet() && commands==previousCommands);
    // A full hour offline must keep saving locally, with no modem commands.
    elapse(CELLULAR_BACKOFF_MS);
    assert(saves>=20 && commands==previousCommands);
    scenario=SUCCESS;
    assert(ensurePdpAndNet() && hasRed && !cellularBackoffActive);
    assert(attempts==4);
  }
  std::cout << "PASS scenario " << argv[1] << " saves=" << saves << "\n";
}
'''
with tempfile.TemporaryDirectory(prefix="hiri-offline-test-") as tmp:
    source = Path(tmp) / "offline.cpp"
    binary = Path(tmp) / "offline.exe"
    source.write_text(stubs + local + connection + cases, encoding="utf-8")
    subprocess.run(["g++", "-std=c++11", str(source), "-o", str(binary)], check=True)
    for scenario in range(7):
        subprocess.run([str(binary), str(scenario)], check=True)
    # Exercise the actual synchronous AT wait helpers, including clock wrap.
    at_start = main.index("bool atRun(const String &cmd", main.index("// -------------------- AT Helpers"))
    at_helpers = main[at_start:main.index("void updateNetworkInfo()", at_start)]
    at_stubs = r'''
struct { bool active=false; uint32_t deadline=0; String resp; } at;
void feedWdt() {}
void atBegin(const String&,const String&,const String&,uint32_t timeout) {
  at.active=true; at.deadline=millis()+timeout; at.resp="timeout";
}
bool atTick(bool &done,bool &ok) {
  done=at.active && (int32_t)(millis()-at.deadline)>=0;
  ok=false;
  if(done) at.active=false;
  return done;
}
'''
    at_cases = r'''
int main() {
  clockMs=179500;
  assert(!atRun("", "OK", "ERROR", 1000));
  assert(saves==1 && samples>0 && pmsReads>0 && !at.active);
  String response;
  clockMs=0xffffff00U;
  lastSdSave=clockMs-179500;
  uint32_t start=millis();
  assert(!sendAtSync("", response, 1000));
  assert(millis()-start==1000 && saves==2 && response=="timeout");
  std::cout << "PASS AT waits: SD serviced and clock wrap handled\n";
}
'''
    source.write_text(stubs[:stubs.index("bool sendAtSync(")] + local + at_stubs + at_helpers + at_cases, encoding="utf-8")
    subprocess.run(["g++", "-std=c++11", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
