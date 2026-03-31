// // ESP8266 Ground Station — 0.96" SSD1306 HUD (REL/MSL big, SATS+HDOP top)
// // Accepts both:
// //   old: t,REL,MSL,sats,T,RH
// //   new: t,REL,MSL,sats,hdop,T,RH

// #include <Arduino.h>
// #include <Wire.h>
// #include <SoftwareSerial.h>
// #include <U8g2lib.h>

// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// // If SH1106, swap to: U8G2_SH1106_128X64_NONAME_F_HW_I2C ...

// #define LORA_RX_PIN 13   // D7: LoRa TXD -> here
// #define LORA_TX_PIN 15   // D8: defined but DO NOT WIRE (boot pin)
// SoftwareSerial LoRaSerial(LORA_RX_PIN, LORA_TX_PIN); // RX, TX(unused)

// #ifndef LED_BUILTIN
// #define LED_BUILTIN 2
// #endif

// static uint32_t lastRxMs = 0, rxBytes = 0;
// static long REL_ft = 0, MSL_ft = 0;
// static float T_C = NAN, RH = NAN, HDOP = NAN;
// static int SATS = 0;

// const uint8_t PAD_L   = 2;
// const uint8_t PAD_R   = 2;
// const uint8_t WIDTH   = 128;
// const uint8_t HEIGHT  = 64;

// const uint8_t Y_TOP    = 9;     // status (SATS/HDOP)
// const uint8_t Y_REL    = 30;    // REL baseline
// const uint8_t Y_MSL    = 52;    // MSL baseline
// const uint8_t Y_BOTTOM = 62;    // T/RH baseline

// #define FONT_SMALL u8g2_font_5x8_tr
// #define FONT_LABEL u8g2_font_6x12_tr
// #define FONT_NUM   u8g2_font_logisoso16_tf

// static void splitCSV(const String& s, String* f, int n) {
//   int start = 0, idx = 0;
//   while (idx < n - 1) {
//     int comma = s.indexOf(',', start);
//     if (comma < 0) break;
//     f[idx++] = s.substring(start, comma);
//     start = comma + 1;
//   }
//   f[idx++] = s.substring(start);
//   while (idx < n) f[idx++] = "";
// }

// static long  toLong (const String& x){ return x.length()? x.toInt()   : 0;   }
// static float toFloat(const String& x){ return x.length()? x.toFloat() : NAN; }

// static void drawTopStatus() {
//   u8g2.setFont(FONT_SMALL);
//   char left[32], right[32];

//   // left: SATS
//   snprintf(left, sizeof(left),  "SAT:%d", SATS);

//   // right: HDOP
//   if (isnan(HDOP)) snprintf(right, sizeof(right), "HDOP:--");
//   else             snprintf(right, sizeof(right), "HDOP:%.2f", HDOP);

//   u8g2.drawStr(PAD_L, Y_TOP, left);
//   int rw = u8g2.getStrWidth(right);
//   u8g2.drawStr(WIDTH - PAD_R - rw, Y_TOP, right);
// }

// static void drawLabelValueFt(uint8_t yBase, const char* label, long value) {
//   u8g2.setFont(FONT_LABEL);
//   u8g2.drawStr(PAD_L, yBase, label);

//   char num[16]; snprintf(num, sizeof(num), "%ld", value);

//   u8g2.setFont(FONT_NUM);
//   int numW = u8g2.getStrWidth(num);

//   u8g2.setFont(FONT_LABEL);
//   const char* unit = "ft";
//   int unitW = u8g2.getStrWidth(unit);

//   int totalW = numW + 4 + unitW;
//   int rightX = WIDTH - PAD_R;
//   int numX   = rightX - totalW;
//   if (numX < PAD_L + 34) numX = PAD_L + 34;

//   u8g2.setFont(FONT_NUM);
//   u8g2.drawStr(numX, yBase, num);

//   u8g2.setFont(FONT_LABEL);
//   u8g2.drawStr(numX + numW + 4, yBase, unit);
// }

// static void drawBottomTRH() {
//   u8g2.setFont(FONT_LABEL);
//   char line[40];
//   if (isnan(T_C) && isnan(RH)) {
//     snprintf(line, sizeof(line), "T --.-C   RH --.-%%");
//   } else if (isnan(T_C)) {
//     snprintf(line, sizeof(line), "T --.-C   RH %.1f%%", RH);
//   } else if (isnan(RH)) {
//     snprintf(line, sizeof(line), "T %.1fC   RH --.-%%", T_C);
//   } else {
//     snprintf(line, sizeof(line), "T %.1fC   RH %.1f%%", T_C, RH);
//   }
//   int w = u8g2.getStrWidth(line);
//   int x = (WIDTH - w) / 2; if (x < PAD_L) x = PAD_L;
//   u8g2.drawStr(x, Y_BOTTOM, line);
// }

// static void drawHUD() {
//   u8g2.clearBuffer();
//   drawTopStatus();
//   drawLabelValueFt(Y_REL, "REL", REL_ft);
//   drawLabelValueFt(Y_MSL, "MSL", MSL_ft);

//   // Faint link-lost hint
//   if (!(lastRxMs && (millis() - lastRxMs) < 4000)) {
//     u8g2.setFont(FONT_SMALL);
//     const char* lost = "LINK LOST";
//     int w = u8g2.getStrWidth(lost);
//     u8g2.drawStr((WIDTH - w)/2, 36, lost);
//   }

//   drawBottomTRH();
//   u8g2.sendBuffer();
// }

// void setup() {
//   pinMode(LED_BUILTIN, OUTPUT);
//   digitalWrite(LED_BUILTIN, HIGH);

//   Serial.begin(115200);
//   u8g2.begin();

//   u8g2.clearBuffer();
//   u8g2.setFont(FONT_LABEL);
//   u8g2.drawStr(PAD_L, 24, "Ground Station");
//   u8g2.setFont(FONT_SMALL);
//   u8g2.drawStr(PAD_L, 36, "LoRa RX=D7 @9600");
//   u8g2.drawStr(PAD_L, 48, "Waiting for CSV...");
//   u8g2.sendBuffer();

//   LoRaSerial.begin(9600);
//   LoRaSerial.listen();
//   LoRaSerial.setTimeout(50);
// }

// void loop() {
//   static String line;
//   while (LoRaSerial.available()) {
//     int c = LoRaSerial.read();
//     if (c < 0) break;
//     rxBytes++;
//     Serial.write(c);

//     if (c == '\n') {
//       String s = line; line = "";
//       s.trim();
//       if (s.length()) {
//         // Try new 7-field first, then fallback to old 6-field
//         // new: t,REL,MSL,sats,hdop,T,RH
//         // old: t,REL,MSL,sats,T,RH
//         const int MAXF = 7;
//         String f[MAXF]; splitCSV(s, f, MAXF);

//         long rel = 0, msl = 0; int sats = 0; float t=NAN, rh=NAN, hdop=NAN;

//         // Decide by comma count:
//         int commas = 0; for (int i=0;i<(int)s.length();++i) if (s[i]==',') commas++;
//         if (commas >= 6) {
//           // 7 fields
//           rel  = toLong (f[1]);
//           msl  = toLong (f[2]);
//           sats = toLong (f[3]);
//           hdop = toFloat(f[4]);
//           t    = toFloat(f[5]);
//           rh   = toFloat(f[6]);
//         } else {
//           // 6 fields (legacy)
//           rel  = toLong (f[1]);
//           msl  = toLong (f[2]);
//           sats = toLong (f[3]);
//           hdop = NAN;                 // not provided in legacy
//           t    = toFloat(f[4]);
//           rh   = toFloat(f[5]);
//         }

//         REL_ft = rel;
//         MSL_ft = msl;
//         SATS   = sats;
//         HDOP   = hdop;
//         T_C    = t;
//         RH     = rh;

//         lastRxMs = millis();
//         digitalWrite(LED_BUILTIN, LOW); delay(6); digitalWrite(LED_BUILTIN, HIGH);
//       }
//     } else if (c != '\r') {
//       if (line.length() < 96) line += (char)c; else line = "";
//     }
//   }

//   static uint32_t lastDraw = 0;
//   if (millis() - lastDraw >= 50) { lastDraw = millis(); drawHUD(); }
// }
/////////////////// 2nd itteration 
// ESP8266 Ground Station — 0.96" SSD1306 HUD (REL/MSL big, SATS+HDOP top)
// Accepts both:
//   old: t,REL,MSL,sats,T,RH
//   new: t,REL,MSL,sats,hdop,T,RH

// #include <Arduino.h>
// #include <Wire.h>
// #include <SoftwareSerial.h>
// #include <U8g2lib.h>

// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// // If SH1106, swap to: U8G2_SH1106_128X64_NONAME_F_HW_I2C ...

// #define LORA_RX_PIN 13   // D7: LoRa TXD -> here
// #define LORA_TX_PIN 15   // D8: defined but DO NOT WIRE (boot pin)
// SoftwareSerial LoRaSerial(LORA_RX_PIN, LORA_TX_PIN); // RX, TX(unused)

// #ifndef LED_BUILTIN
// #define LED_BUILTIN 2
// #endif

// static uint32_t lastRxMs = 0, rxBytes = 0;
// static long REL_ft = 0, MSL_ft = 0;
// static float T_C = NAN, RH = NAN, HDOP = NAN;
// static int SATS = 0;

// // Launch site pad altitude above mean sea level (ft).
// // Change this to your actual field elevation. If usePadMSL is true,
// // displayed MSL = padMSL_ft + REL_ft.
// static long padMSL_ft = 850;
// static bool usePadMSL = true;

// // Display smoothing so numbers don't jitter by a couple of feet
// static long REL_disp_ft = 0, MSL_disp_ft = 0;
// static bool REL_disp_init = false, MSL_disp_init = false;
// const long ALT_DEADBAND_FT = 2;  // change to 1 if you want it more twitchy

// const uint8_t PAD_L   = 2;
// const uint8_t PAD_R   = 2;
// const uint8_t WIDTH   = 128;
// const uint8_t HEIGHT  = 64;

// const uint8_t Y_TOP    = 9;     // status (SATS/HDOP)
// const uint8_t Y_REL    = 30;    // REL baseline
// const uint8_t Y_MSL    = 52;    // MSL baseline
// const uint8_t Y_BOTTOM = 62;    // T/RH baseline

// #define FONT_SMALL u8g2_font_5x8_tr
// #define FONT_LABEL u8g2_font_6x12_tr
// #define FONT_NUM   u8g2_font_logisoso16_tf

// static void splitCSV(const String& s, String* f, int n) {
//   int start = 0, idx = 0;
//   while (idx < n - 1) {
//     int comma = s.indexOf(',', start);
//     if (comma < 0) break;
//     f[idx++] = s.substring(start, comma);
//     start = comma + 1;
//   }
//   if (idx < n) f[idx] = s.substring(start);
// }

// static long toLong(const String& x){ return x.length()? x.toInt() : 0; }
// static float toFloat(const String& x){ return x.length()? x.toFloat() : NAN; }

// static void drawTopStatus() {
//   u8g2.setFont(FONT_SMALL);
//   char left[32], right[32];

//   // left: SATS
//   snprintf(left, sizeof(left),  "SAT:%d", SATS);

//   // right: HDOP
//   if (isnan(HDOP)) snprintf(right, sizeof(right), "HDOP:--");
//   else             snprintf(right, sizeof(right), "HDOP:%.2f", HDOP);

//   u8g2.drawStr(PAD_L, Y_TOP, left);
//   int rw = u8g2.getStrWidth(right);
//   u8g2.drawStr(WIDTH - PAD_R - rw, Y_TOP, right);
// }

// static void drawLabelValueFt(uint8_t yBase, const char* label, long value) {
//   u8g2.setFont(FONT_LABEL);
//   u8g2.drawStr(PAD_L, yBase, label);

//   char num[16]; snprintf(num, sizeof(num), "%ld", value);

//   u8g2.setFont(FONT_NUM);
//   int numW = u8g2.getStrWidth(num);

//   u8g2.setFont(FONT_LABEL);
//   const char* unit = "ft";
//   int unitW = u8g2.getStrWidth(unit);

//   int xNum  = WIDTH - PAD_R - unitW - 2 - numW;
//   int xUnit = WIDTH - PAD_R - unitW;

//   u8g2.setFont(FONT_NUM);
//   u8g2.drawStr(xNum,  yBase, num);
//   u8g2.setFont(FONT_LABEL);
//   u8g2.drawStr(xUnit, yBase, unit);
// }

// static void drawBottomTRH() {
//   u8g2.setFont(FONT_SMALL);
//   char buf[32];

//   if (!isnan(T_C)) {
//     snprintf(buf, sizeof(buf), "T:%.1fC", T_C);
//     u8g2.drawStr(PAD_L, Y_BOTTOM, buf);
//   } else {
//     u8g2.drawStr(PAD_L, Y_BOTTOM, "T:--");
//   }

//   if (!isnan(RH)) {
//     snprintf(buf, sizeof(buf), "RH:%.0f%%", RH);
//   } else {
//     snprintf(buf, sizeof(buf), "RH:--");
//   }
//   int rw = u8g2.getStrWidth(buf);
//   u8g2.drawStr(WIDTH - PAD_R - rw, Y_BOTTOM, buf);
// }

// static void drawHUD() {
//   u8g2.clearBuffer();
//   drawTopStatus();

//   // Decide what MSL value to show: either rocket-computed MSL, or
//   // pad altitude (ft) + REL (ft) if usePadMSL is true.
//   long relRaw = REL_ft;
//   long mslRaw = usePadMSL ? (padMSL_ft + REL_ft) : MSL_ft;

//   // Simple deadband smoothing so the display doesn't jitter by a foot or two.
//   if (!REL_disp_init) {
//     REL_disp_ft   = relRaw;
//     REL_disp_init = true;
//   } else {
//     long d = relRaw - REL_disp_ft; if (d < 0) d = -d;
//     if (d >= ALT_DEADBAND_FT) REL_disp_ft = relRaw;
//   }

//   if (!MSL_disp_init) {
//     MSL_disp_ft   = mslRaw;
//     MSL_disp_init = true;
//   } else {
//     long d = mslRaw - MSL_disp_ft; if (d < 0) d = -d;
//     if (d >= ALT_DEADBAND_FT) MSL_disp_ft = mslRaw;
//   }

//   drawLabelValueFt(Y_REL, "REL", REL_disp_ft);
//   drawLabelValueFt(Y_MSL, "MSL", MSL_disp_ft);

//   // Faint link-lost hint
//   if (!(lastRxMs && (millis() - lastRxMs) < 4000)) {
//     u8g2.setFont(FONT_SMALL);
//     const char* lost = "LINK LOST";
//     int w = u8g2.getStrWidth(lost);
//     u8g2.drawStr((WIDTH - w)/2, 36, lost);
//   }

//   drawBottomTRH();
//   u8g2.sendBuffer();
// }

// void setup() {
//   pinMode(LED_BUILTIN, OUTPUT);
//   digitalWrite(LED_BUILTIN, HIGH);

//   Serial.begin(115200);
//   u8g2.begin();

//   u8g2.clearBuffer();
//   u8g2.setFont(FONT_LABEL);
//   u8g2.drawStr(PAD_L, 24, "Ground Station");
//   u8g2.setFont(FONT_SMALL);
//   u8g2.drawStr(PAD_L, 36, "LoRa RX=D7 @9600");
//   u8g2.drawStr(PAD_L, 48, "Waiting for CSV...");
//   u8g2.sendBuffer();

//   LoRaSerial.begin(9600);
//   LoRaSerial.listen();
//   LoRaSerial.setTimeout(50);
// }

// void loop() {
//   static String line;
//   while (LoRaSerial.available()) {
//     int c = LoRaSerial.read();
//     if (c < 0) break;
//     rxBytes++;
//     Serial.write(c);

//     if (c == '\n') {
//       String s = line; line = "";
//       s.trim();
//       if (s.length()) {
//         // Try new 7-field first, then fallback to old 6-field
//         // new: t,REL,MSL,sats,hdop,T,RH
//         // old: t,REL,MSL,sats,T,RH
//         const int MAXF = 7;
//         String f[MAXF]; splitCSV(s, f, MAXF);

//         long rel = 0, msl = 0; int sats = 0; float t=NAN, rh=NAN, hdop=NAN;

//         // Decide by comma count:
//         int commas = 0; for (int i=0;i<(int)s.length();++i) if (s[i]==',') commas++;
//         if (commas >= 6) {
//           // 7 fields
//           rel  = toLong (f[1]);
//           msl  = toLong (f[2]);
//           sats = toLong (f[3]);
//           hdop = toFloat(f[4]);
//           t    = toFloat(f[5]);
//           rh   = toFloat(f[6]);
//         } else {
//           // 6 fields (legacy)
//           rel  = toLong (f[1]);
//           msl  = toLong (f[2]);
//           sats = toLong (f[3]);
//           hdop = NAN;                 // not provided in legacy
//           t    = toFloat(f[4]);
//           rh   = toFloat(f[5]);
//         }

//         REL_ft = rel;
//         MSL_ft = msl;
//         SATS   = sats;
//         HDOP   = hdop;
//         T_C    = t;
//         RH     = rh;

//         lastRxMs = millis();
//         digitalWrite(LED_BUILTIN, LOW); delay(6); digitalWrite(LED_BUILTIN, HIGH);
//       }
//     } else if (c != '\r') {
//       if (line.length() < 96) line += (char)c; else line = "";
//     }
//   }

//   static uint32_t lastDraw = 0;
//   if (millis() - lastDraw >= 50) { lastDraw = millis(); drawHUD(); }
// }
///////////////// dual gps method

/*
  GROUND STATION - ESP8266
  Beginner-friendly version with its own GPS (simple "DGPS-lite" for altitude)

  FEATURES:
  - Reads LoRa telemetry from rocket:
      timeMs,rocketRelAltFeet,rocketMslFeetRaw,rocketSats,rocketHdop,rocketTempC,rocketHumidity
  - Reads GPS on the ground station:
      groundGpsAltMeters, groundSats, groundHdop
  - You set truePadAltitudeFeet from a map/website
  - Computes altitudeErrorFeet = truePadAltitudeFeet - groundGpsAltitudeFeet
  - Corrected rocket MSL: rocketMslFeetCorrected = rocketMslFeetRaw + altitudeErrorFeet
  - Displays all this on an OLED
*/

/*
  GROUND STATION - ESP8266
  Beginner-friendly version with its own GPS (simple "DGPS-lite" for altitude)

  FEATURES:
  - Reads LoRa telemetry from rocket:
      timeMs,rocketRelAltFeet,rocketMslFeetRaw,rocketSats,rocketHdop,rocketTempC,rocketHumidity
  - Reads GPS on the ground station:
      groundGpsAltMeters, groundSats, groundHdop
  - You set truePadAltitudeFeet from a map/website
  - Computes altitudeErrorFeet = truePadAltitudeFeet - groundGpsAltitudeFeet
  - Corrected rocket MSL: rocketMslFeetCorrected = rocketMslFeetRaw + altitudeErrorFeet
  - Displays all this on an OLED
*/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <U8g2lib.h>
#include <TinyGPSPlus.h>

// =========================== DISPLAY SETUP ===========================

// SSD1306 128x64 on I2C (adjust if SH1106, etc.)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// =========================== LORA SERIAL (RX from rocket) ===========================

// LoRa RX on D7 (GPIO 13), TX on D8 (GPIO 15, usually unused)
const int LORA_RX_PIN = 13;  // D7
const int LORA_TX_PIN = 15;  // D8 (not actually used)
SoftwareSerial loraSerial(LORA_RX_PIN, LORA_TX_PIN); // RX, TX

// =========================== GROUND GPS SERIAL ===========================

// Using HardwareSerial for GPS on ESP8266
// Example: RX on D1 (GPIO 5), TX on D2 (GPIO 4)
const int GPS_RX_PIN = 5;  // GPS TX -> ESP8266 RX
const int GPS_TX_PIN = 4;  // GPS RX <- ESP8266 TX
HardwareSerial groundGpsSerial(1); // NOTE: on some ESP8266 cores, Serial1 is TX-only; adjust if needed
TinyGPSPlus groundGps;

// =========================== CONSTANTS ===========================

const float METERS_TO_FEET = 3.28084f;

// This is your true pad altitude above sea level in feet (from map/website).
// Example: 850 ft MSL
const float truePadAltitudeFeet = 850.0f;

// Telemetry CSV field count from rocket
const int ROCKET_FIELD_COUNT = 7;

// =========================== ROCKET TELEMETRY STATE ===========================

uint32_t lastRocketPacketTimeMs = 0;

long rocketRelAltFeet        = 0;
long rocketMslFeetRaw        = 0;
long rocketMslFeetCorrected  = 0;
int  rocketSatelliteCount    = 0;
float rocketHdop             = NAN;
float rocketTemperatureC     = NAN;
float rocketHumidityPct      = NAN;

// =========================== GROUND GPS STATE ===========================

bool  groundGpsAltEmaInitialized = false;
float groundGpsAltitudeEma_m     = NAN;
int   groundSatelliteCount       = 0;
float groundHdop                 = NAN;
float altitudeErrorFeet          = 0.0f;  // correction to apply to rocket MSL
bool  altitudeCorrectionValid    = false;

// =========================== HELPER: SPLIT CSV ===========================

void splitCsvLine(const String &line, String *fields, int fieldCount) {
  int startIndex = 0;
  int currentField = 0;

  while (currentField < fieldCount - 1) {
    int commaIndex = line.indexOf(',', startIndex);
    if (commaIndex < 0) break;
    fields[currentField++] = line.substring(startIndex, commaIndex);
    startIndex = commaIndex + 1;
  }

  if (currentField < fieldCount) {
    fields[currentField] = line.substring(startIndex);
  }
}

// =========================== HELPER: EMA ===========================

float updateEma(float oldValue, float newValue, float alpha, bool &isInitialized) {
  if (!isInitialized || isnan(oldValue)) {
    isInitialized = true;
    return newValue;
  }
  return alpha * newValue + (1.0f - alpha) * oldValue;
}

// =========================== HELPER: GROUND GPS QUALITY ===========================

bool groundGpsAltitudeIsGood() {
  if (!groundGps.location.isValid())   return false;
  if (!groundGps.altitude.isValid())   return false;
  if (!groundGps.hdop.isValid())       return false;
  if (!groundGps.satellites.isValid()) return false;

  int   sats = groundGps.satellites.value();
  float hdopValue = groundGps.hdop.hdop();

  if (sats < 7)      return false;
  if (hdopValue > 1.5f) return false;

  return true;
}

// =========================== DRAW HUD ===========================

void drawHud() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tr);

  // ---- 1) Top line: Rocket SATS + HDOP ----
  char topLeft[32];
  snprintf(topLeft, sizeof(topLeft), "R SAT:%d", rocketSatelliteCount);
  display.drawStr(0, 10, topLeft);

  char topRight[32];
  if (isnan(rocketHdop)) {
    snprintf(topRight, sizeof(topRight), "R HDOP:--");
  } else {
    snprintf(topRight, sizeof(topRight), "R HDOP:%.2f", rocketHdop);
  }
  int wTR = display.getStrWidth(topRight);
  display.drawStr(128 - wTR, 10, topRight);

  // ---- 2) Relative altitude (rocket) ----
  char relLine[32];
  snprintf(relLine, sizeof(relLine), "REL: %ld ft", rocketRelAltFeet);
  display.drawStr(0, 28, relLine);

  // ---- 3) Corrected MSL altitude ----
  char mslLine[32];
  if (altitudeCorrectionValid) {
    snprintf(mslLine, sizeof(mslLine), "MSL*: %ld ft", rocketMslFeetCorrected);
  } else {
    snprintf(mslLine, sizeof(mslLine), "MSL: %ld ft", rocketMslFeetRaw);
  }
  display.drawStr(0, 42, mslLine);

  // ---- 4) Temperature and Humidity from rocket ----
  char tempLine[32];
  if (isnan(rocketTemperatureC)) {
    snprintf(tempLine, sizeof(tempLine), "T: -- C");
  } else {
    snprintf(tempLine, sizeof(tempLine), "T: %.1f C", rocketTemperatureC);
  }
  display.drawStr(0, 58, tempLine);

  char rhLine[32];
  if (isnan(rocketHumidityPct)) {
    snprintf(rhLine, sizeof(rhLine), "RH: --%%");
  } else {
    snprintf(rhLine, sizeof(rhLine), "RH: %.0f%%", rocketHumidityPct);
  }
  int wRH = display.getStrWidth(rhLine);
  display.drawStr(128 - wRH, 58, rhLine);

  // ---- 5) Ground GPS status (very small, top-middle) ----
  display.setFont(u8g2_font_5x8_tr);
  char groundLine[32];
  if (groundSatelliteCount > 0) {
    if (altitudeCorrectionValid) {
      snprintf(groundLine, sizeof(groundLine), "BASE OK dH=%.1fft", altitudeErrorFeet);
    } else {
      snprintf(groundLine, sizeof(groundLine), "BASE S:%d HDOP:%.2f", groundSatelliteCount, groundHdop);
    }
  } else {
    snprintf(groundLine, sizeof(groundLine), "BASE GPS: --");
  }
  int wGL = display.getStrWidth(groundLine);
  display.drawStr((128 - wGL) / 2, 22, groundLine);

  // ---- 6) Link lost indicator ----
  if (lastRocketPacketTimeMs == 0 || millis() - lastRocketPacketTimeMs > 4000) {
    const char *msg = "ROCKET LINK LOST";
    int w = display.getStrWidth(msg);
    display.drawStr((128 - w) / 2, 36, msg);
  }

  display.sendBuffer();
}

// =========================== SETUP ===========================

void setup() {
  Serial.begin(115200);

  // Display setup
  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tr);
  display.drawStr(0, 20, "Ground Station");
  display.drawStr(0, 35, "Starting...");
  display.sendBuffer();

  // LoRa (from rocket)
  loraSerial.begin(9600);
  loraSerial.listen();

  // Ground GPS serial
  // NOTE: On some ESP8266 boards, Serial1 is TX-only.
  // If so, you may need a SoftwareSerial instead for GPS.
  groundGpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  delay(1000);
}

// =========================== LOOP ===========================

void loop() {
  uint32_t now = millis();

  // ---- 1) Read ground GPS ----
  while (groundGpsSerial.available()) {
    groundGps.encode(groundGpsSerial.read());
  }

  if (groundGpsAltitudeIsGood()) {
    // Update ground GPS EMA altitude
    float groundAltRaw_m = groundGps.altitude.meters();
    groundGpsAltitudeEma_m = updateEma(groundGpsAltitudeEma_m, groundAltRaw_m, 0.1f, groundGpsAltEmaInitialized);

    groundSatelliteCount = groundGps.satellites.isValid() ? (int)groundGps.satellites.value() : 0;
    groundHdop           = groundGps.hdop.isValid() ? groundGps.hdop.hdop() : NAN;

    // Compute altitude correction once we have a stable EMA
    if (groundGpsAltEmaInitialized && !isnan(groundGpsAltitudeEma_m)) {
      float groundGpsAltitudeFeet = groundGpsAltitudeEma_m * METERS_TO_FEET;
      altitudeErrorFeet        = truePadAltitudeFeet - groundGpsAltitudeFeet;
      altitudeCorrectionValid  = true;
    }
  } else {
    // Ground GPS not good enough
    groundSatelliteCount = groundGps.satellites.isValid() ? (int)groundGps.satellites.value() : 0;
    groundHdop           = groundGps.hdop.isValid() ? groundGps.hdop.hdop() : NAN;
    // Don't trust correction if GPS is bad
    // altitudeCorrectionValid remains as last good OR false at start
  }

  // ---- 2) Read LoRa telemetry from rocket ----
  static String line;
  while (loraSerial.available()) {
    int c = loraSerial.read();
    if (c < 0) break;

    if (c == '\n') {
      String msg = line;
      line = "";
      msg.trim();

      if (msg.length() > 0) {
        Serial.println(msg); // debug

        String fields[ROCKET_FIELD_COUNT];
        splitCsvLine(msg, fields, ROCKET_FIELD_COUNT);

        // Expected format:
        // 0: timeMs
        // 1: rocketRelAltFeet
        // 2: rocketMslFeetRaw
        // 3: rocketSats
        // 4: rocketHdop
        // 5: rocketTempC
        // 6: rocketHumidity
        rocketRelAltFeet     = fields[1].toInt();
        rocketMslFeetRaw     = fields[2].toInt();
        rocketSatelliteCount = fields[3].toInt();
        rocketHdop           = fields[4].toFloat();
        rocketTemperatureC   = fields[5].toFloat();
        rocketHumidityPct    = fields[6].toFloat();

        // Apply correction if we have it
        if (altitudeCorrectionValid) {
          rocketMslFeetCorrected = rocketMslFeetRaw + (long)lroundf(altitudeErrorFeet);
        } else {
          rocketMslFeetCorrected = rocketMslFeetRaw;
        }

        lastRocketPacketTimeMs = now;
      }
    } else if (c != '\r') {
      if (line.length() < 120) {
        line += (char)c;
      } else {
        line = "";
      }
    }
  }

  // ---- 3) Redraw HUD ~20 times per second ----
  static uint32_t lastDrawMs = 0;
  if (now - lastDrawMs > 50) {
    lastDrawMs = now;
    drawHud();
  }
}

