// /*
//   ESP32 Flight (LoRa TX @9600) — REL pad-lock fixed + 7-sat GPS chooser
//   Sends once/second: t_ms,REL_ft,MSL_ft,sats,T_C,RH_pct

//   Wiring:
//     I2C:        SDA=21, SCL=22
//     GPS UART1:  GPS TX -> GPIO16 (RX1), GPS RX <- GPIO17 (TX1)  @9600
//     LoRa UART2: LoRa TXD -> GPIO4  (RX2), LoRa RXD <- GPIO2  (TX2)  @9600
//                 (E32/E220 Normal mode: M0=GND, M1=GND)
//     SD (optional): CS=GPIO5
// */

// #include <Wire.h>
// #include <SPI.h>
// #include <SD.h>
// #include <Adafruit_Sensor.h>
// #include <Adafruit_BME280.h>
// #include <Adafruit_MPU6050.h>
// #include <TinyGPSPlus.h>
// #include <HardwareSerial.h>
// #include <math.h>

// // ---------- Pins ----------
// #define I2C_SDA_PIN   21
// #define I2C_SCL_PIN   22
// #define GPS_RX_PIN    16
// #define GPS_TX_PIN    17
// #define GPS_BAUD_DEF  9600
// #define LORA_RX2      4
// #define LORA_TX2      2
// #define LORA_BAUD     9600
// #define SD_CS         5

// #ifndef LED_BUILTIN
// #define LED_BUILTIN 2
// #endif

// // ---------- Tunables ----------
// #define SERIAL_BAUD        115200
// #define SAMPLE_PERIOD_MS   1000

// // pressure smoothing
// #define PRESS_BURST_N       9
// #define PRESS_BURST_DELAY   8
// #define PRESS_EMA_ALPHA     0.20f

// // REL pad-lock (zero when calm)
// #define PADLOCK_WINDOW_SEC  5
// #define PADLOCK_SIGMA_FT    0.30f
// #define PADLOCK_MAX_WAIT_S  30

// // boot-time MSL decision
// const uint32_t MSL_DECIDE_DELAY_MS = 10000; // ~10 s after boot
// const float    GPS_ALT_EMA_ALPHA   = 0.15f;

// // GPS quality thresholds
// #define GPS_MIN_SATS   7       // <= your request: choose GPS from 7 satellites
// #define GPS_MAX_HDOP   1.2f
// #define GPS_MAX_AGE_MS 1500

// // ---------- Objects/state ----------
// Adafruit_BME280 bme;
// Adafruit_MPU6050 mpu;
// TinyGPSPlus gps;
// HardwareSerial GPSSerial(1);
// HardwareSerial LoRaSerial(2);

// // pressure filter
// bool  pressEmaInit = false;
// float pressEmaPa   = NAN;

// // REL baseline state
// bool     relLocked        = false;
// bool     relAssessStarted = false;  // <-- used by z command and pad-lock logic
// float    p0_rel_Pa        = NAN;
// float    p0_candidate     = NAN;
// uint32_t startAssessMs    = 0;

// // SD logs
// File rawFile, procFile;
// const char* RAW_PATH  = "/raw.csv";
// const char* PROC_PATH = "/proc.csv";

// // MSL chooser
// uint32_t bootMs = 0;
// bool     mslSourceDecided = false;
// bool     useGpsForMSL = false; // set at ~10 s
// float    gpsAltEma_m = NAN;

// // ---------- Helpers ----------
// static inline float baroAlt_m_from_p0(float pPa, float p0Pa) {
//   if (pPa <= 0 || isnan(p0Pa)) return NAN;
//   return 8434.5f * logf(p0Pa / pPa);
// }
// static inline long ft_round(float ft) { return (long)lroundf(ft); }

// float stddev_ft(const float* v, int n) {
//   if (n <= 1) return INFINITY;
//   double s=0, s2=0;
//   for (int i=0;i<n;i++){ s+=v[i]; s2+= (double)v[i]*v[i]; }
//   double m = s/n;
//   double v2 = (s2/n) - m*m;
//   return (float)((v2>0)? sqrt(v2) : 0.0);
// }

// bool gpsAltQualityGood() {
//   if (!gps.altitude.isValid() || !gps.hdop.isValid()) return false;
//   int   sats = gps.satellites.isValid()? (int)gps.satellites.value() : 0;
//   float hdop = gps.hdop.hdop();
//   if (sats < GPS_MIN_SATS) return false;
//   if (!(hdop > 0) || hdop > GPS_MAX_HDOP) return false;
//   if (gps.altitude.age() > GPS_MAX_AGE_MS) return false; // ms since last fix
//   return true;
// }

// float smoothedPressurePa() {
//   float v[PRESS_BURST_N];
//   for (int i=0;i<PRESS_BURST_N;i++){ v[i]=bme.readPressure(); delay(PRESS_BURST_DELAY); }
//   // median
//   for (int i=0;i<PRESS_BURST_N-1;i++)
//     for (int j=i+1;j<PRESS_BURST_N;j++)
//       if (v[j] < v[i]) { float t=v[i]; v[i]=v[j]; v[j]=t; }
//   float med = v[PRESS_BURST_N/2];
//   if (!pressEmaInit){ pressEmaPa = med; pressEmaInit = true; }
//   else { pressEmaPa = (1.0f-PRESS_EMA_ALPHA)*pressEmaPa + PRESS_EMA_ALPHA*med; }
//   return pressEmaPa;
// }

// // SD/logs minimal headers
// bool openWithHeader(File& f, const char* path, const __FlashStringHelper* header) {
//   bool need = !SD.exists(path);
//   f = SD.open(path, FILE_WRITE);
//   if (!f) return false;
//   if (need) { f.println(header); f.flush(); }
//   return true;
// }

// void initSD() {
//   Serial.print(F("# SD init... "));
//   if (!SD.begin(SD_CS)) { Serial.println(F("fail (logging off)")); return; }
//   Serial.println(F("ok"));
//   openWithHeader(rawFile,  RAW_PATH,  F("time_ms,P_Pa,T_C,RH_pct,sats,gpsAlt_m"));
//   openWithHeader(procFile, PROC_PATH, F("time_ms,P_hPa,REL_ft,MSL_ft,T_C,RH_pct,sats"));
// }

// // ---------- Serial commands ----------
// void handleSerialCmds() {
//   while (Serial.available()) {
//     char c = (char)Serial.read();
//     if (c=='z' || c=='Z') {
//       // FULL re-zero: allow new assessment snapshot (fixes the “always 0” issue)
//       relLocked        = false;
//       relAssessStarted = false;  // <- key change
//       p0_rel_Pa        = NAN;
//       p0_candidate     = NAN;
//       startAssessMs    = millis();
//       Serial.println(F("# REL baseline cleared (z): re-assessing..."));
//     } else if (c=='p' || c=='P') {
//       Serial.print(F("# p0_rel_Pa=")); Serial.println(p0_rel_Pa,1);
//     }
//   }
// }

// // ---------- setup ----------
// void setup() {
//   pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW);
//   Serial.begin(SERIAL_BAUD); while(!Serial){}

//   Serial.println(F("# Flight TX (REL pad-lock fix + 7-sat GPS chooser)"));
//   Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
//   Wire.setClock(100000);

//   if (!(bme.begin(0x76) || bme.begin(0x77))) {
//     Serial.println(F("BME280 not found")); while(1) delay(1000);
//   }
//   if (!mpu.begin(0x68)) {
//     Serial.println(F("MPU6050 @0x68 failed")); while(1) delay(1000);
//   }
//   mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
//   mpu.setGyroRange(MPU6050_RANGE_500_DEG);

//   GPSSerial.begin(GPS_BAUD_DEF, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
//   LoRaSerial.begin(LORA_BAUD,   SERIAL_8N1, LORA_RX2,   LORA_TX2);

//   initSD();

//   startAssessMs = millis();
//   bootMs        = startAssessMs;
// }

// // ---------- loop ----------
// void loop() {
//   handleSerialCmds();

//   // feed GPS
//   while (GPSSerial.available()) gps.encode(GPSSerial.read());

//   // 1 Hz tick
//   static uint32_t tPrev=0; uint32_t now=millis();
//   if (now - tPrev < SAMPLE_PERIOD_MS) return;
//   tPrev = now;

//   // sensors
//   float PPa = smoothedPressurePa();
//   float PH  = PPa/100.0f;
//   float T   = bme.readTemperature();
//   float RH  = bme.readHumidity();
//   sensors_event_t acc,gyr,tmp; mpu.getEvent(&acc,&gyr,&tmp);

//   int sats = gps.satellites.isValid()? (int)gps.satellites.value() : 0;

//   // keep GPS altitude EMA only when quality is good
//   if (gpsAltQualityGood()) {
//     float alt_m = (float)gps.altitude.meters();
//     if (isnan(gpsAltEma_m)) gpsAltEma_m = alt_m;
//     else gpsAltEma_m = (1.0f - GPS_ALT_EMA_ALPHA)*gpsAltEma_m + GPS_ALT_EMA_ALPHA*alt_m;
//   }

//   // ---- REL pad-lock (baro baseline) ----
//   static float relWindowFt[PADLOCK_WINDOW_SEC];
//   static int   relCount = 0;

//   if (!relLocked) {
//     // Start assessment ONCE (don’t keep resetting each tick)
//     if (!relAssessStarted) {
//       p0_candidate     = PPa;              // snapshot current pressure
//       relAssessStarted = true;
//       relCount         = 0;
//       startAssessMs    = now;              // restart timer for timeout window
//       Serial.println(F("# Assessing REL baseline... hold still"));
//     }

//     // Compute REL vs the fixed candidate baseline
//     float rel_m  = baroAlt_m_from_p0(PPa, p0_candidate);
//     float rel_ft = rel_m * 3.28084f;

//     // rolling window of last PADLOCK_WINDOW_SEC seconds
//     if (relCount < PADLOCK_WINDOW_SEC) {
//       relWindowFt[relCount++] = rel_ft;
//     } else {
//       for (int i = 1; i < PADLOCK_WINDOW_SEC; i++) relWindowFt[i - 1] = relWindowFt[i];
//       relWindowFt[PADLOCK_WINDOW_SEC - 1] = rel_ft;
//     }

//     // decide lock
//     if (relCount >= 3) {
//       int   n     = (relCount < PADLOCK_WINDOW_SEC) ? relCount : PADLOCK_WINDOW_SEC;
//       float sigma = stddev_ft(relWindowFt, n);
//       bool  quiet   = (sigma <= PADLOCK_SIGMA_FT);
//       bool  timeout = ((now - startAssessMs) >= (PADLOCK_MAX_WAIT_S * 1000UL));

//       if (quiet || timeout) {
//         p0_rel_Pa = p0_candidate;          // freeze the baseline at the candidate
//         relLocked = true;
//         Serial.print(F("# REL baseline locked "));
//         if (quiet)   Serial.print(F("(stable) "));
//         if (timeout) Serial.print(F("(timeout) "));
//         Serial.print(F("p0_rel_Pa=")); Serial.println(p0_rel_Pa, 1);
//       }
//     }
//   }

//   // compute REL (use locked baseline if available; else the candidate)
//   long REL_ft = 0;
//   {
//     float p0_use = relLocked ? p0_rel_Pa : p0_candidate;
//     float rel_m  = (!isnan(p0_use)) ? baroAlt_m_from_p0(PPa, p0_use) : NAN;
//     REL_ft = (!isnan(rel_m)) ? ft_round(rel_m * 3.28084f) : 0;
//   }

//   // ---- one-time MSL source decision ~10 s after boot ----
//   if (!mslSourceDecided && (now - bootMs >= MSL_DECIDE_DELAY_MS)) {
//     useGpsForMSL = gpsAltQualityGood();
//     mslSourceDecided = true;
//     Serial.print(F("# MSL source: "));
//     Serial.println(useGpsForMSL ? F("GPS (EMA)") : F("BARO (std p0)"));
//   }

//   // compute MSL from chosen source, with fallback
//   long MSL_ft = 0;
//   if (mslSourceDecided && useGpsForMSL) {
//     if (!isnan(gpsAltEma_m)) {
//       MSL_ft = ft_round(gpsAltEma_m * 3.28084f);
//     } else {
//       float p0Pa = 1013.25f * 100.0f;
//       float m_msl = baroAlt_m_from_p0(PPa, p0Pa);
//       MSL_ft = ft_round(m_msl * 3.28084f);
//     }
//   } else {
//     float p0Pa = 1013.25f * 100.0f;          // std SL pressure
//     float m_msl = baroAlt_m_from_p0(PPa, p0Pa);
//     MSL_ft = ft_round(m_msl * 3.28084f);
//   }

//   // SD logs (millisecond time for dense graphs)
//   if (rawFile)  rawFile.printf("%lu,%.2f,%.2f,%.2f,%d,%.2f\n",
//                   (unsigned long)now, PPa, T, RH, sats,
//                   gps.altitude.isValid()? (float)gps.altitude.meters() : NAN);
//   if (procFile) procFile.printf("%lu,%.2f,%ld,%ld,%.2f,%.2f,%d\n",
//                   (unsigned long)now, PH, REL_ft, MSL_ft, T, RH, sats);

//   // LoRa CSV (6 fields expected by GS)
//   LoRaSerial.printf("%lu,%ld,%ld,%d,%.2f,%.2f\n",
//     (unsigned long)now, REL_ft, MSL_ft, sats, T, RH);

//   // mirror to USB for sanity
//   Serial.printf("TX: %lu,%ld,%ld,%d,%.2f,%.2f\n",
//     (unsigned long)now, REL_ft, MSL_ft, sats, T, RH);
// }
////////////////// second itteration
/*
  ESP32 Flight (LoRa TX @9600) — REL pad-lock fixed + 7-sat GPS chooser
  Sends once/second: t_ms,REL_ft,MSL_ft,sats,hdop,T_C,RH_pct

  Wiring:
    I2C:        SDA=21, SCL=22
    GPS UART1:  GPS TX -> GPIO16 (RX1), GPS RX <- GPIO17 (TX1)  @9600
    LoRa UART2: LoRa TXD -> GPIO4  (RX2), LoRa RXD <- GPIO2  (TX2)  @9600
                (E32/E220 Normal mode: M0=GND, M1=GND)
    SD (optional): CS=GPIO5
*/

// #include <Wire.h>
// #include <SPI.h>
// #include <SD.h>
// #include <Adafruit_Sensor.h>
// #include <Adafruit_BME280.h>
// #include <Adafruit_MPU6050.h>
// #include <TinyGPSPlus.h>

// #define SD_CS        5
// #define SEALEVEL_HPA 1013.25

// // LoRa Serial
// HardwareSerial LoRaSerial(2);

// // GPS Serial
// HardwareSerial GPSSerial(1);

// // BME / MPU
// Adafruit_BME280 bme;
// Adafruit_MPU6050 mpu;
// TinyGPSPlus gps;

// // flight config
// const uint32_t SAMPLE_PERIOD_MS     = 1000;
// const uint32_t MSL_DECIDE_DELAY_MS  = 10000; // 10 s after boot

// // pressure EMA
// bool  pressEmaInit = false;
// float pressEmaPa   = NAN;

// // REL baseline state
// bool     relLocked        = false;
// bool     relAssessStarted = false;  // used by z command and pad-lock logic
// float    p0_rel_Pa        = NAN;
// float    p0_candidate     = NAN;
// uint32_t startAssessMs    = 0;

// // SD logs
// File rawFile, procFile;
// const char* RAW_PATH  = "/raw.csv";
// const char* PROC_PATH = "/proc.csv";

// // MSL chooser
// uint32_t bootMs = 0;
// bool     mslSourceDecided = false;
// bool     useGpsForMSL = false; // set at ~10 s
// float    gpsAltEma_m = NAN;

// // ---------- Helpers ----------
// static inline float baroAlt_m_from_p0(float pPa, float p0Pa) {
//   if (!(pPa > 0 && p0Pa > 0)) return NAN;
//   return 44330.0f * (1.0f - powf(pPa / p0Pa, 0.1903f));
// }

// static inline long ft_round(float ft) { return (long)lroundf(ft); }

// static float smoothedPressurePa() {
//   float p = bme.readPressure();
//   if (!pressEmaInit) {
//     pressEmaPa   = p;
//     pressEmaInit = true;
//   } else {
//     const float ALPHA = 0.2f;
//     pressEmaPa = ALPHA * p + (1.0f - ALPHA) * pressEmaPa;
//   }
//   return pressEmaPa;
// }

// // basic stddev helper
// float stddev_ft(float* arr, int n) {
//   if (n <= 1) return 0.0f;
//   float sum = 0, sum2 = 0;
//   for (int i = 0; i < n; i++) {
//     sum  += arr[i];
//     sum2 += arr[i] * arr[i];
//   }
//   float mean = sum / n;
//   float var  = (sum2 / n) - (mean * mean);
//   return (var > 0) ? sqrtf(var) : 0.0f;
// }

// // SD/logs minimal headers
// bool openWithHeader(File& f, const char* path, const __FlashStringHelper* header) {
//   bool need = !SD.exists(path);
//   f = SD.open(path, FILE_WRITE);
//   if (!f) return false;
//   if (need) { f.println(header); f.flush(); }
//   return true;
// }

// void initSD() {
//   Serial.print(F("# SD init... "));
//   if (!SD.begin(SD_CS)) { Serial.println(F("fail (logging off)")); return; }
//   Serial.println(F("ok"));
//   openWithHeader(rawFile,  RAW_PATH,  F("time_ms,P_Pa,T_C,RH_pct,sats,gpsAlt_m,hdop"));
//   openWithHeader(procFile, PROC_PATH, F("time_ms,P_hPa,REL_ft,MSL_ft,T_C,RH_pct,sats"));
// }

// // GPS quality heuristics for altitude
// bool gpsAltQualityGood() {
//   if (!gps.location.isValid())   return false;
//   if (!gps.altitude.isValid())   return false;
//   if (!gps.hdop.isValid())       return false;
//   if (gps.satellites.isValid() && gps.satellites.value() < 7) return false;
//   if (gps.hdop.hdop() > 1.5f)    return false;
//   return true;
// }

// // ---------- Serial commands ----------
// void handleSerialCmds() {
//   while (Serial.available()) {
//     char c = (char)Serial.read();
//     if (c=='z' || c=='Z') {
//       // FULL re-zero: allow new assessment snapshot
//       relLocked        = false;
//       relAssessStarted = false;
//       p0_rel_Pa        = NAN;
//       p0_candidate     = NAN;
//       Serial.println(F("# REL reset requested"));
//     }
//   }
// }

// void setup() {
//   Serial.begin(115200);
//   delay(2000);
//   Serial.println(F("\n# ESP32 Flight starting..."));

//   Wire.begin();
//   if (!bme.begin(0x76)) {
//     Serial.println(F("# BME280 not found!"));
//   }
//   if (!mpu.begin()) {
//     Serial.println(F("# MPU6050 not found!"));
//   }

//   // GPS serial
//   GPSSerial.begin(9600, SERIAL_8N1, 16, 17);

//   // LoRa serial (E32/E220 in UART mode @9600)
//   LoRaSerial.begin(9600, SERIAL_8N1, 4, 2);

//   // SD
//   initSD();

//   bootMs = millis();

//   Serial.println(F("# Ready. z/Z to reset REL baseline"));
// }

// // constants for REL pad lock
// const uint32_t PADLOCK_WINDOW_SEC   = 8;
// const float    PADLOCK_SIGMA_FT     = 1.5f;
// const uint32_t PADLOCK_MAX_WAIT_S   = 20;

// void loop() {
//   handleSerialCmds();

//   // feed GPS
//   while (GPSSerial.available()) gps.encode(GPSSerial.read());

//   // 1 Hz tick
//   static uint32_t tPrev=0; uint32_t now=millis();
//   if (now - tPrev < SAMPLE_PERIOD_MS) return;
//   tPrev = now;

//   // sensors
//   float PPa = smoothedPressurePa();
//   float PH  = PPa/100.0f;
//   float T   = bme.readTemperature();
//   float RH  = bme.readHumidity();
//   sensors_event_t acc,gyr,tmp; mpu.getEvent(&acc,&gyr,&tmp);

//   int   sats = gps.satellites.isValid()? (int)gps.satellites.value() : 0;
//   float hdop = gps.hdop.isValid()? gps.hdop.hdop() : NAN;

//   // keep GPS altitude EMA only when quality is good
//   if (gpsAltQualityGood()) {
//     float alt_m = gps.altitude.meters();
//     if (isnan(gpsAltEma_m)) gpsAltEma_m = alt_m;
//     else {
//       const float GPS_ALT_EMA_ALPHA = 0.15f;
//       gpsAltEma_m = GPS_ALT_EMA_ALPHA * alt_m + (1.0f - GPS_ALT_EMA_ALPHA) * gpsAltEma_m;
//     }
//   }

//   // ---- REL pad-lock logic ----
//   static float    relWindowFt[PADLOCK_WINDOW_SEC];
//   static int      relCount = 0;

//   if (!relLocked) {
//     if (!relAssessStarted) {
//       // first time we get here since reset: snapshot p0_candidate, start timer
//       p0_candidate     = PPa;
//       startAssessMs    = now;
//       relAssessStarted = true;
//       relCount         = 0;
//       Serial.print(F("# REL assess start p0_candidate="));
//       Serial.println(p0_candidate, 1);
//     } else {
//       // update candidate baseline as simple EMA to ride out drift
//       const float P0_ALPHA = 0.05f;
//       p0_candidate = P0_ALPHA * PPa + (1.0f - P0_ALPHA) * p0_candidate;

//       // Compute REL vs the fixed candidate baseline
//       float rel_m  = baroAlt_m_from_p0(PPa, p0_candidate);
//       float rel_ft = rel_m * 3.28084f;

//       // rolling window of last PADLOCK_WINDOW_SEC seconds
//       if (relCount < PADLOCK_WINDOW_SEC) {
//         relWindowFt[relCount++] = rel_ft;
//       } else {
//         for (int i = 1; i < PADLOCK_WINDOW_SEC; i++) relWindowFt[i - 1] = relWindowFt[i];
//         relWindowFt[PADLOCK_WINDOW_SEC - 1] = rel_ft;
//       }

//       // decide lock
//       if (relCount >= 3) {
//         int   n     = (relCount < PADLOCK_WINDOW_SEC) ? relCount : PADLOCK_WINDOW_SEC;
//         float sigma = stddev_ft(relWindowFt, n);
//         bool  quiet   = (sigma <= PADLOCK_SIGMA_FT);
//         bool  timeout = ((now - startAssessMs) >= (PADLOCK_MAX_WAIT_S * 1000UL));

//         if (quiet || timeout) {
//           p0_rel_Pa = p0_candidate;          // freeze the baseline at the candidate
//           relLocked = true;
//           Serial.print(F("# REL baseline locked "));
//           if (quiet)   Serial.print(F("(stable) "));
//           if (timeout) Serial.print(F("(timeout) "));
//           Serial.print(F("p0_rel_Pa=")); Serial.println(p0_rel_Pa, 1);
//         }
//       }
//     }
//   }

//   // compute REL (use locked baseline if available; else the candidate)
//   long REL_ft = 0;
//   {
//     float p0_use = relLocked ? p0_rel_Pa : p0_candidate;
//     float rel_m  = (!isnan(p0_use)) ? baroAlt_m_from_p0(PPa, p0_use) : NAN;
//     REL_ft = (!isnan(rel_m)) ? ft_round(rel_m * 3.28084f) : 0;
//   }

//   // ---- one-time MSL source decision ~10 s after boot ----
//   if (!mslSourceDecided && (now - bootMs >= MSL_DECIDE_DELAY_MS)) {
//     useGpsForMSL = gpsAltQualityGood();
//     mslSourceDecided = true;
//     Serial.print(F("# MSL source: "));
//     Serial.println(useGpsForMSL ? F("GPS (EMA)") : F("BARO (std p0)"));
//   }

//   // compute MSL from chosen source, with fallback
//   long MSL_ft = 0;
//   if (mslSourceDecided && useGpsForMSL) {
//     if (!isnan(gpsAltEma_m)) {
//       MSL_ft = ft_round(gpsAltEma_m * 3.28084f);
//     } else {
//       float p0Pa = 1013.25f * 100.0f;
//       float m_msl = baroAlt_m_from_p0(PPa, p0Pa);
//       MSL_ft = ft_round(m_msl * 3.28084f);
//     }
//   } else {
//     float p0Pa = 1013.25f * 100.0f;          // std SL pressure
//     float m_msl = baroAlt_m_from_p0(PPa, p0Pa);
//     MSL_ft = ft_round(m_msl * 3.28084f);
//   }

//   // SD logs (millisecond time for dense graphs)
//   if (rawFile)  rawFile.printf("%lu,%.2f,%.2f,%.2f,%d,%.2f,%.2f\n",
//                   (unsigned long)now, PPa, T, RH, sats,
//                   gps.altitude.isValid()? (float)gps.altitude.meters() : NAN,
//                   hdop);
//   if (procFile) procFile.printf("%lu,%.2f,%ld,%ld,%.2f,%.2f,%d\n",
//                   (unsigned long)now, PH, REL_ft, MSL_ft, T, RH, sats);

//   // Periodic flush so we don't lose data if power is cut
//   static uint32_t lastFlushMs = 0;
//   if (now - lastFlushMs >= 250) {
//     if (rawFile)  rawFile.flush();
//     if (procFile) procFile.flush();
//     lastFlushMs = now;
//   }

//   // LoRa CSV (7 fields): t_ms,REL_ft,MSL_ft,sats,hdop,T_C,RH_pct
//   LoRaSerial.printf("%lu,%ld,%ld,%d,%.2f,%.2f,%.2f\n",
//     (unsigned long)now, REL_ft, MSL_ft, sats, hdop, T, RH);

//   // mirror to USB for sanity
//   Serial.printf("TX: %lu,%ld,%ld,%d,%.2f,%.2f,%.2f\n",
//     (unsigned long)now, REL_ft, MSL_ft, sats, hdop, T, RH);
// }
// dual gps method
/*
  FLIGHT COMPUTER - ESP32
  Beginner-friendly version

  FEATURES:
  - Reads BME280: pressure, temperature, humidity
  - Reads GPS: satellites, HDOP, altitude
  - Computes:
      relativeAltitudeFeet = altitude above launch pad
      mslAltitudeFeetRaw   = GPS-based altitude above sea level (fallback to baro)
  - Sends telemetry over LoRa (CSV)
  - Logs data to SD card (flight_log.csv)
*/

/*
  FLIGHT COMPUTER - ESP32
  Beginner-friendly version

  FEATURES:
  - Reads BME280: pressure, temperature, humidity
  - Reads GPS: satellites, HDOP, altitude
  - Computes:
      relativeAltitudeFeet = altitude above launch pad
      mslAltitudeFeetRaw   = GPS-based altitude above sea level (fallback to baro)
  - Sends telemetry over LoRa (CSV)
  - Logs data to SD card (flight_log.csv)
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_BME280.h>
#include <TinyGPSPlus.h>

// =========================== PIN DEFINITIONS ===========================

// I2C for BME280: SDA = 21, SCL = 22 (default for ESP32)
const int SD_CHIP_SELECT_PIN = 5;

// GPS uses HardwareSerial 1 (on pins 16,17)
HardwareSerial gpsSerial(1);
const int GPS_RX_PIN = 16; // GPS TX -> ESP32 RX
const int GPS_TX_PIN = 17; // GPS RX -> ESP32 TX

// LoRa uses HardwareSerial 2 (on pins 4,2)
HardwareSerial loraSerial(2);
const int LORA_RX_PIN = 4; // LoRa TX -> ESP32 RX
const int LORA_TX_PIN = 2; // LoRa RX -> ESP32 TX

// =========================== OBJECTS ===========================

Adafruit_BME280 bme;
TinyGPSPlus gps;
File logFile;

// =========================== CONSTANTS ===========================

const float SEALEVEL_PRESSURE_HPA_STANDARD = 1013.25f;  // standard sea-level pressure

const uint32_t SAMPLE_PERIOD_MS   = 1000;   // 1 Hz loop
const uint32_t BASELINE_TIME_MS   = 10000;  // first 10 seconds to learn pad pressure
const float    METERS_TO_FEET     = 3.28084f;

// =========================== STATE VARIABLES ===========================

// Time
uint32_t bootTimeMs = 0;

// Baseline pressure for relative altitude (pad pressure)
bool     baselineReady         = false;
uint32_t baselineStartTimeMs   = 0;
double   baselinePressureSumPa = 0.0;
uint32_t baselineSampleCount   = 0;
float    baselinePressurePa    = NAN;

// EMA pressure filter (to smooth sensor noise)
bool  pressureEmaInitialized = false;
float pressureEmaPa          = NAN;

// EMA GPS altitude
bool  gpsAltEmaInitialized = false;
float gpsAltitudeEma_m     = NAN;

// =========================== HELPER FUNCTIONS ===========================

// Convert pressure + baseline pressure into altitude difference (meters)
float altitudeFromPressure(float currentPressurePa, float baselinePressurePa) {
  if (currentPressurePa <= 0 || baselinePressurePa <= 0) return NAN;
  // Standard barometric formula approximation
  return 44330.0f * (1.0f - powf(currentPressurePa / baselinePressurePa, 0.1903f));
}

// Simple EMA filter
float updateEma(float oldValue, float newValue, float alpha, bool &isInitialized) {
  if (!isInitialized || isnan(oldValue)) {
    isInitialized = true;
    return newValue;
  }
  return alpha * newValue + (1.0f - alpha) * oldValue;
}

// Initialize SD and log file
void initSdLogging() {
  Serial.print(F("SD init... "));
  if (!SD.begin(SD_CHIP_SELECT_PIN)) {
    Serial.println(F("FAILED (no SD logging)"));
    return;
  }
  Serial.println(F("OK"));

  logFile = SD.open("/flight_log.csv", FILE_WRITE);
  if (!logFile) {
    Serial.println(F("ERROR: Could not open flight_log.csv"));
    return;
  }

  // If file is empty, write header
  if (logFile.size() == 0) {
    logFile.println("timeMs,pressurePa,tempC,humidityPct,relativeAltitudeFeet,mslAltitudeFeetRaw,sats,hdop,gpsAltMeters");
    logFile.flush();
  }
}

// Decide if we trust GPS altitude right now
bool gpsAltitudeIsGood() {
  if (!gps.location.isValid())   return false;
  if (!gps.altitude.isValid())   return false;
  if (!gps.hdop.isValid())       return false;
  if (!gps.satellites.isValid()) return false;

  int   satelliteCount = gps.satellites.value();
  float hdopValue      = gps.hdop.hdop();

  // Basic thresholds (you can adjust these)
  if (satelliteCount < 7)   return false;
  if (hdopValue > 1.5f)     return false;

  return true;
}

// =========================== SETUP ===========================

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println(F("=== ESP32 FLIGHT COMPUTER START ==="));

  Wire.begin();

  // BME280
  if (!bme.begin(0x76)) {
    Serial.println(F("ERROR: BME280 not found at 0x76"));
  } else {
    Serial.println(F("BME280 OK"));
  }

  // GPS serial
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println(F("GPS serial started"));

  // LoRa serial
  loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  Serial.println(F("LoRa serial started"));

  // SD logging
  initSdLogging();

  bootTimeMs         = millis();
  baselineStartTimeMs = bootTimeMs;
}

// =========================== MAIN LOOP ===========================

void loop() {
  // ---- 1) Feed GPS parser ----
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // ---- 2) Run at 1 Hz ----
  static uint32_t lastSampleTimeMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastSampleTimeMs < SAMPLE_PERIOD_MS) {
    return;
  }
  lastSampleTimeMs = nowMs;

  // ---- 3) Read BME280 (pressure, temperature, humidity) ----
  float rawPressurePa = bme.readPressure();
  float temperatureC  = bme.readTemperature();
  float humidityPct   = bme.readHumidity();

  // Smooth pressure with EMA so relative altitude doesn't jump wildly
  pressureEmaPa = updateEma(pressureEmaPa, rawPressurePa, 0.2f, pressureEmaInitialized);
  float smoothedPressurePa = pressureEmaPa;

  // ---- 4) Learn baseline pressure for first 10 seconds ----
  if (!baselineReady) {
    if (nowMs - baselineStartTimeMs <= BASELINE_TIME_MS) {
      baselinePressureSumPa += smoothedPressurePa;
      baselineSampleCount++;
    } else {
      if (baselineSampleCount > 0) {
        baselinePressurePa = (float)(baselinePressureSumPa / (double)baselineSampleCount);
        baselineReady = true;
        Serial.print(F("Baseline pressure locked at "));
        Serial.print(baselinePressurePa);
        Serial.println(F(" Pa"));
      }
    }
  }

  // ---- 5) Compute relative altitude (feet above pad) ----
  long relativeAltitudeFeet = 0;
  if (baselineReady) {
    float relativeAltitude_m = altitudeFromPressure(smoothedPressurePa, baselinePressurePa);
    float relativeAltitude_ft = relativeAltitude_m * METERS_TO_FEET;
    relativeAltitudeFeet = (long)lroundf(relativeAltitude_ft);
  } else {
    relativeAltitudeFeet = 0; // before baseline, treat as 0
  }

  // ---- 6) GPS data and EMA altitude ----
  int   satelliteCount = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  float hdopValue      = gps.hdop.isValid() ? gps.hdop.hdop() : NAN;
  float gpsAltMetersRaw = gps.altitude.isValid() ? gps.altitude.meters() : NAN;

  if (gpsAltitudeIsGood()) {
    gpsAltitudeEma_m = updateEma(gpsAltitudeEma_m, gpsAltMetersRaw, 0.15f, gpsAltEmaInitialized);
  }

  // ---- 7) Compute MSL altitude in feet (raw) ----
  long mslAltitudeFeetRaw = 0;

  if (gpsAltitudeIsGood() && gpsAltEmaInitialized && !isnan(gpsAltitudeEma_m)) {
    // Use GPS EMA as main MSL altitude
    float mslFeet = gpsAltitudeEma_m * METERS_TO_FEET;
    mslAltitudeFeetRaw = (long)lroundf(mslFeet);
  } else {
    // Fallback: barometric MSL using standard sea-level pressure
    float p0Pa = SEALEVEL_PRESSURE_HPA_STANDARD * 100.0f;
    float alt_m = altitudeFromPressure(smoothedPressurePa, p0Pa);
    float alt_ft = alt_m * METERS_TO_FEET;
    mslAltitudeFeetRaw = (long)lroundf(alt_ft);
  }

  // ---- 8) Write to SD log (if available) ----
  if (logFile) {
    logFile.printf(
      "%lu,%.2f,%.2f,%.2f,%ld,%ld,%d,%.2f,%.2f\n",
      (unsigned long)nowMs,
      smoothedPressurePa,
      temperatureC,
      humidityPct,
      relativeAltitudeFeet,
      mslAltitudeFeetRaw,
      satelliteCount,
      hdopValue,
      gpsAltMetersRaw
    );
  }

  // Periodically flush so data is actually written
  static uint32_t lastFlushMs = 0;
  if (nowMs - lastFlushMs >= 250) {
    if (logFile) logFile.flush();
    lastFlushMs = nowMs;
  }

  // ---- 9) Send CSV over LoRa ----
  // Format:
  // timeMs,relativeAltitudeFeet,mslAltitudeFeetRaw,satellites,hdop,temperatureC,humidityPct
  loraSerial.printf(
    "%lu,%ld,%ld,%d,%.2f,%.2f,%.2f\n",
    (unsigned long)nowMs,
    relativeAltitudeFeet,
    mslAltitudeFeetRaw,
    satelliteCount,
    hdopValue,
    temperatureC,
    humidityPct
  );

  // Also print to USB serial for debugging
  Serial.printf(
    "TX: %lu,%ld,%ld,%d,%.2f,%.2f,%.2f\n",
    (unsigned long)nowMs,
    relativeAltitudeFeet,
    mslAltitudeFeetRaw,
    satelliteCount,
    hdopValue,
    temperatureC,
    humidityPct
  );
}


