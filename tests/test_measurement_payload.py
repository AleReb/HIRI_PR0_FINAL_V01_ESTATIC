"""Validate the real URL builder without sending measurements to the backend."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
main = (root / "HIRI_PR0_FINAL_V01_ESTATIC.ino").read_text(encoding="utf-8")
helpers = (root / "helpers.ino").read_text(encoding="utf-8")
builder = main[main.index("String getIdsSensores("):main.index("bool sendCurrentMeasurement() {", main.index("String getIdsSensores("))]
constants = "\n".join(re.findall(r'^const char \*(?:API_BASE|STATIC_API_BASE|STATIC_IDS_VARIABLES|GLOBAL_IDS_VARIABLES) = .*;', main, re.M))
safe_helpers = helpers[:helpers.index("// -------------------- Watchdog")]
stubs = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <iostream>
#include <algorithm>
using std::isnan; using std::isinf;
bool isDigit(char c) { return c>='0' && c<='9'; }
struct String : std::string {
  using std::string::string;
  String(const std::string &s): std::string(s) {}
  String(uint32_t v): std::string(std::to_string(v)) {}
  String(int v): std::string(std::to_string(v)) {}
  String(float v,int decimals) { char b[64]; snprintf(b,sizeof(b),"%.*f",decimals,v); assign(b); }
  long toInt() const { try { return std::stol(*this); } catch(...) { return 0; } }
};
bool GasOK=true, ENS160OK=true,ens160DataValid=true,SDS198OK=true,SHT4xOK=true,SHT31OK=true;
float pmsTempC=21.5,pmsHum=45.25,tempsht4x=22.125,humsht4x=46.5;
float tempsht31=99,humsht31=98,batV=4;
uint16_t PM1=11,PM25=22,PM10=33,ens160Tvoc=1,ens160Eco2=2;
int csq=15,SDS198PM100=44;
String gpsLat="-33",gpsLon="-70",gpsSpeedKmh="0",satellitesStr="5";
struct { float readGasConcentrationPPM() { return 1; } } gas;
'''
cases = r'''
int main() {
  const String expected="https://api-sensores.cmasccp.cl/insertarMedicion?idsSensores=1232,1232,1232,1232,1232,1233,1233&idsVariables=3,6,7,8,9,3,6&valores=21.500,45.250,11,22,33,22.125,46.500";
  assert(buildMeasurementUrl("14")==expected);
  for(int id=1;id<=13;++id) {
    String url=buildMeasurementUrl(String(id));
    assert(url.find("http://")==0);
    assert(url.find(std::string("&idsVariables=")+GLOBAL_IDS_VARIABLES)!=std::string::npos);
    auto start=url.find("&valores=")+9;
    assert(std::count(url.begin()+start,url.end(),',')==16);
    String ids=getIdsSensores(String(id));
    assert(std::count(ids.begin(),ids.end(),',')==16);
  }
  SHT4xOK=false;
  String url=buildMeasurementUrl("14");
  assert(url.substr(url.find("&valores="))=="&valores=21.500,45.250,11,22,33,-1,-1");
  pmsTempC=NAN; pmsHum=INFINITY;
  url=buildMeasurementUrl("14");
  assert(url.substr(url.find("&valores="))=="&valores=-1,-1,11,22,33,-1,-1");
  assert(buildMeasurementUrl("15").empty());
  assert(buildMeasurementUrl("0").empty());
  std::cout << "PASS: device 14 URL/order, legacy 1-13, missing data, unknown IDs\n";
}
'''
with tempfile.TemporaryDirectory(prefix="hiri-payload-test-") as tmp:
    source = Path(tmp) / "payload.cpp"
    binary = Path(tmp) / "payload.exe"
    source.write_text(stubs + constants + safe_helpers + builder + cases, encoding="utf-8")
    subprocess.run(["g++", "-std=c++11", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
