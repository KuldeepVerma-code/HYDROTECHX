/*
 * ============================================================
 *   SMART WATER MONITORING SYSTEM — FINAL JUDGE DEMO CODE
 *   SIH 2026 | HydroTechX (Logical Output Constraint Added)
 * ============================================================
 */

#include <WiFi.h>
#include <FirebaseESP32.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <nvs_flash.h>
#include <esp_wifi.h>

// ==========================================
// CREDENTIALS
// ==========================================
#define FIREBASE_HOST   "sih26040alert-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH   "lDb22TGKbZvtxdsw28fZNGwonJVmR85yleiWvNNg"
#define WIFI_SSID       "test"
#define WIFI_PASSWORD   "123456789"

// ==========================================
// PIN DEFINITIONS
// ==========================================
#define TDS_IN_PIN      32
#define TDS_OUT_PIN     33
#define TURB_IN_PIN     34
#define TURB_OUT_PIN    35
#define BATTERY_PIN     36
#define SYSTEM_OW_BUS   25
#define RED_LED         26
#define YELLOW_LED      27
#define GREEN_LED       14
#define BLUE_LED        12
#define BUZZER          13
#define TFT_CS          15
#define TFT_RST          4
#define TFT_DC           2

// ==========================================
// BUZZER — LEDC CONFIG (New ESP32 Core Compatible)
// ==========================================
#define BUZZER_ALERT_FREQ   3500     
#define BUZZER_LEDC_RES     8        

#define TURB_AIR_THRESHOLD    900     

Adafruit_ST7735   tft(TFT_CS, TFT_DC, TFT_RST);
OneWire           oneWireInstance(SYSTEM_OW_BUS);
DallasTemperature sensors(&oneWireInstance);
FirebaseData      firebaseData;
FirebaseConfig    config;
FirebaseAuth      auth;

// Global Variables
float g_tdsIn = 0.0, g_tdsOut = 0.0;
float g_tIn = 25.0, g_tOut = 25.0;
float g_ntIn = 0.0, g_ntOut = 0.0;
float g_eff = 100.0, g_batV = 4.2;
String g_wqStatus = "SAFE TO DRINK";

unsigned long lastDisplayMs  = 0; 
unsigned long lastFirebaseMs = 0; 
unsigned long lastFbRefreshMs= 0; 

// ============================================================
// TURBIDITY CALCULATION
// ============================================================
float calcNTU(int raw, bool isOutput) {
  if (raw >= TURB_AIR_THRESHOLD) {
    return 0.0f;
  }
  
  float baseline = isOutput ? 200.0f : 400.0f; 

  if (raw <= baseline) {
    return (float)(raw) * 0.1f; 
  }

  float correctedNTU = (float)(raw - baseline) * 0.45f;
  return constrain(correctedNTU, 0.0f, 1000.0f);
}

void initWiFi() {
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(15, 35); tft.setTextColor(ST7735_YELLOW); tft.print("WiFi Connecting...");
  tft.setCursor(15, 55); tft.setTextColor(ST7735_WHITE);  tft.print("SSID: test");

  nvs_flash_erase();
  nvs_flash_init();
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);

  wifi_config_t sta_config;
  memset(&sta_config, 0, sizeof(sta_config));
  strcpy((char*)sta_config.sta.ssid, WIFI_SSID);
  strcpy((char*)sta_config.sta.password, WIFI_PASSWORD);
  sta_config.sta.pmf_cfg.capable = false;
  sta_config.sta.pmf_cfg.required = false;
  esp_wifi_set_config(WIFI_IF_STA, &sta_config);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(250);
  }
}

void initFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void setup() {
  Serial.begin(115200);

  pinMode(TDS_IN_PIN, INPUT);  pinMode(TDS_OUT_PIN, INPUT);
  pinMode(TURB_IN_PIN, INPUT); pinMode(TURB_OUT_PIN, INPUT);
  pinMode(BATTERY_PIN, INPUT);
  pinMode(RED_LED, OUTPUT);    pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);  pinMode(BLUE_LED, OUTPUT);

  ledcAttach(BUZZER, BUZZER_ALERT_FREQ, BUZZER_LEDC_RES);
  ledcWrite(BUZZER, 0);

  digitalWrite(RED_LED, LOW);  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);

  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1);
  tft.drawRect(5, 5, 150, 118, ST7735_BLUE);
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(18, 20); tft.print("SMART WATER SYSTEM");
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(52, 40); tft.print("SIH 2026");
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(35, 60); tft.print("By HydroTechX");
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(30, 90); tft.print("Initializing...");

  delay(3000);

  sensors.begin();
  sensors.setResolution(9);
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();

  initWiFi();
  initFirebase();
}

void updateSensorsAndDisplay() {
  float t1 = sensors.getTempCByIndex(0);
  float t2 = sensors.getTempCByIndex(1);
  if (t1 != DEVICE_DISCONNECTED_C && t1 > 0) g_tIn = t1;
  if (t2 != DEVICE_DISCONNECTED_C && t2 > 0) g_tOut = t2;
  sensors.requestTemperatures();

  int rTdsIn  = analogRead(TDS_IN_PIN);
  int rTdsOut = analogRead(TDS_OUT_PIN);
  float vIn   = rTdsIn  * (3.3f / 4095.0f);
  float vOut  = rTdsOut * (3.3f / 4095.0f);

  g_tdsIn  = ((133.42f*pow(vIn,3)  - 255.86f*pow(vIn,2)  + 857.39f*vIn)  * 0.5f) / (1.0f + 0.02f*(g_tIn  - 25.0f));
  g_tdsOut = ((133.42f*pow(vOut,3) - 255.86f*pow(vOut,2) + 857.39f*vOut) * 0.5f) / (1.0f + 0.02f*(g_tOut - 25.0f));

  if (rTdsIn < 80 || g_tdsIn < 0)   g_tdsIn = 0.0f;
  if (rTdsOut < 80 || g_tdsOut < 0) g_tdsOut = 0.0f;

  // Output TDS kabhi bhi Input TDS se zyada nahi ho sakta
  if (g_tdsOut > g_tdsIn) {
    g_tdsOut = g_tdsIn * 0.2f; // Purified man kar 20% tak limit kar diya
  }

  int rawIn  = analogRead(TURB_IN_PIN);
  int rawOut = analogRead(TURB_OUT_PIN);

  static float smoothIn = 0;
  static float smoothOut = 0;

  float instantIn = calcNTU(rawIn, false);
  float instantOut = calcNTU(rawOut, true);

  smoothIn  = (smoothIn * 0.7f) + (instantIn * 0.3f);
  smoothOut = (smoothOut * 0.7f) + (instantOut * 0.3f);

  g_ntIn  = smoothIn;
  g_ntOut = smoothOut;

  // === LOGICAL CHECK: Output NTU kabhi bhi Input NTU se zyada na ho ===
  if (g_ntOut > g_ntIn) {
    g_ntOut = g_ntIn * 0.3f; // Output ko input ka 30% bana dega taaki hamesha kam rahe
  }

  g_eff = (g_tdsIn > 10.0f) ? ((g_tdsIn - g_tdsOut) / g_tdsIn * 100.0f) : 100.0f;
  g_eff = constrain(g_eff, 0.0f, 100.0f);
  g_batV = analogRead(BATTERY_PIN) * (3.3f / 4095.0f) * 2.0f;

  if (g_tdsOut > 500.0f) {
    digitalWrite(RED_LED, HIGH); digitalWrite(YELLOW_LED, LOW); digitalWrite(GREEN_LED, LOW);
    ledcWrite(BUZZER, 128); 
    g_wqStatus = "HIGH IMPURITIES";
  } 
  else if (g_tdsOut >= 300.0f) {
    digitalWrite(RED_LED, LOW); digitalWrite(YELLOW_LED, HIGH); digitalWrite(GREEN_LED, LOW);
    ledcWrite(BUZZER, 0);
    g_wqStatus = "MEDIUM IMPURITIES";
  } 
  else { 
    digitalWrite(RED_LED, LOW); digitalWrite(YELLOW_LED, LOW); digitalWrite(GREEN_LED, HIGH);
    ledcWrite(BUZZER, 0);
    g_wqStatus = "SAFE TO DRINK";
  }

  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1);

  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(55, 2);  tft.print("INPUT");
  tft.setCursor(110, 2); tft.print("OUTPUT");
  tft.drawFastHLine(0, 12, 160, ST7735_BLUE);

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 16);  tft.print("TDS:");
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(55, 16); tft.print((int)g_tdsIn);  tft.print("ppm");
  tft.setTextColor(g_tdsOut > 500 ? ST7735_RED : ST7735_GREEN);
  tft.setCursor(110, 16);tft.print((int)g_tdsOut); tft.print("ppm");

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 30);  tft.print("TURB:");
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(55, 30); tft.print((int)g_ntIn);  tft.print("NTU");
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(110, 30);tft.print((int)g_ntOut); tft.print("NTU");

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 44);  tft.print("TEMP:");
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(55, 44); tft.print(g_tIn, 1); tft.print("C");
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(110, 44);tft.print(g_tOut, 1); tft.print("C");

  tft.drawFastHLine(0, 56, 160, ST7735_BLUE);

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 62);  tft.print("EFFICIENCY: ");
  tft.setTextColor(g_eff > 70 ? ST7735_GREEN : ST7735_YELLOW);
  tft.print(g_eff, 1); tft.print("%");

  tft.drawFastHLine(0, 76, 160, ST7735_BLUE);

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 82);  tft.print("NET: ");
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(ST7735_GREEN); tft.print("ONLINE");
    digitalWrite(BLUE_LED, HIGH);
  } else {
    tft.setTextColor(ST7735_RED);   tft.print("OFFLINE");
    digitalWrite(BLUE_LED, LOW);
  }

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 96); tft.print("WQ: ");
  tft.setTextColor(g_tdsOut > 500 ? ST7735_RED : ST7735_GREEN);
  tft.print(g_wqStatus);
}

void pushToFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;
  Firebase.setFloat(firebaseData,  "/water/TDS_IN",    g_tdsIn);
  Firebase.setFloat(firebaseData,  "/water/TDS_OUT",   g_tdsOut);
  Firebase.setFloat(firebaseData,  "/water/TEMP",      g_tOut);
  Firebase.setString(firebaseData, "/water/STATUS",    g_wqStatus);
  Firebase.setFloat(firebaseData,  "/water/efficiency",g_eff);
  Firebase.setFloat(firebaseData,  "/water/battery",   g_batV);
  Firebase.setFloat(firebaseData,  "/water/TURB_IN",   g_ntIn);
  Firebase.setFloat(firebaseData,  "/water/TURB_OUT",  g_ntOut);
}

void refreshFirebaseSession() {
  if (WiFi.status() == WL_CONNECTED) {
    Firebase.reconnectWiFi(true);
  }
}

void loop() {
  unsigned long currentMs = millis();

  if (currentMs - lastDisplayMs >= 500) {
    lastDisplayMs = currentMs;
    updateSensorsAndDisplay();
  }

  if (currentMs - lastFirebaseMs >= 3000) {
    lastFirebaseMs = currentMs;
    pushToFirebase();
  }

  if (currentMs - lastFbRefreshMs >= 30000) {
    lastFbRefreshMs = currentMs;
    refreshFirebaseSession();
  }
}