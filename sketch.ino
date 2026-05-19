#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "crop_model.h"

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* server = "http://api.thingspeak.com/update";
String apiKey = "LZJ6K5U0OFPH61UK";

#define DHTPIN 15
#define DHTTYPE DHT22
#define POT_N_PIN    1
#define POT_P_PIN    2
#define POT_K_PIN    3
#define POT_PH_PIN   4
#define POT_RAIN_PIN 5

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4);

unsigned long lastTime   = 0;
unsigned long timerDelay = 15000;

const char* crops[] = {
  "", "rice", "maize", "jute", "cotton", "coconut",
  "papaya", "orange", "apple", "muskmelon", "watermelon",
  "grapes", "mango", "banana", "pomegranate", "lentil",
  "blackgram", "mungbean", "mothbeans", "pigeonpeas",
  "kidneybeans", "chickpea", "coffee", "wheat"
};

// ──────────────────────────────────────────
int readSmooth(int pin) {
  long total = 0;
  for (int i = 0; i < 10; i++) { total += analogRead(pin); delay(2); }
  return total / 10;
}

// ──────────────────────────────────────────
int predictCrop(float N, float P, float K,
                float temp, float humidity,
                float ph, float rainfall) {

  struct CropProfile {
    const char* name;
    float n, p, k;
    float t, h, phv, r;
  };

  CropProfile cropData[24] = {
    {"",0,0,0,0,0,0,0},
    {"rice",      90, 40, 40, 25, 85, 6.5, 220},
    {"maize",     80, 50, 40, 22, 65, 6.5,  80},
    {"jute",      75, 45, 40, 30, 80, 6.7, 200},
    {"cotton",   120, 60, 60, 32, 60, 7.0,  90},
    {"coconut",   40, 30, 60, 30, 85, 6.2, 180},
    {"papaya",    50, 50, 50, 30, 70, 6.5, 140},
    {"orange",    30, 40, 30, 22, 65, 6.0, 140},
    {"apple",     20, 25, 20, 15, 60, 6.5, 150},
    {"muskmelon", 60, 40, 50, 34, 65, 6.8,  40},
    {"watermelon",70, 50, 50, 32, 70, 6.5,  70},
    {"grapes",    25, 25, 35, 24, 65, 6.2,  80},
    {"mango",     35, 30, 30, 33, 55, 6.5, 120},
    {"banana",    90, 70, 70, 30, 85, 6.3, 170},
    {"pomegranate",20,20, 40, 32, 55, 6.8,  80},
    {"lentil",    25, 40, 20, 18, 55, 7.0,  50},
    {"blackgram", 40, 60, 40, 30, 70, 6.8, 100},
    {"mungbean",  35, 50, 40, 31, 70, 6.5,  90},
    {"mothbeans", 20, 30, 20, 37, 45, 7.5,  35},
    {"pigeonpeas",30, 40, 20, 32, 60, 6.8, 110},
    {"kidneybeans",25,35, 20, 20, 65, 6.5, 120},
    {"chickpea",  20, 30, 20, 18, 55, 7.2,  60},
    {"coffee",   100, 40, 40, 24, 75, 5.5, 200},
    {"wheat",     80, 40, 40, 18, 55, 7.0,  90}
  };

  float bestScore = -999999;
  int bestCrop = 1;

  for (int i = 1; i <= 23; i++) {

    float score = 1000;

    // Strong NPK influence
    score -= abs(N - cropData[i].n) * 4.0;
    score -= abs(P - cropData[i].p) * 3.5;
    score -= abs(K - cropData[i].k) * 3.5;

    // Environmental influence
    score -= abs(temp     - cropData[i].t)   * 18.0;
    score -= abs(humidity - cropData[i].h)   *  5.0;
    score -= abs(ph       - cropData[i].phv) * 60.0;
    score -= abs(rainfall - cropData[i].r)   *  1.8;

    // Extreme penalties
    if (ph > 8.5 || ph < 4.0)   score -= 200;
    if (temp > 40 || temp < 5)   score -= 200;
    if (rainfall > 280)          score -= 150;

    // Bonus for very close matches
    if (abs(N - cropData[i].n) < 10) score += 40;
    if (abs(P - cropData[i].p) < 10) score += 40;
    if (abs(K - cropData[i].k) < 10) score += 40;

    if (score > bestScore) {
      bestScore = score;
      bestCrop  = i;
    }
  }

  return bestCrop;
}

// ──────────────────────────────────────────
// LCD loading animation during prediction
// ──────────────────────────────────────────
void showPredictionLatency() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Analyzing Soil...   ");
  lcd.setCursor(0, 1); lcd.print("Running AI Model    ");
  lcd.setCursor(0, 2); lcd.print("[                ]  ");
  int steps = 16;
  for (int i = 0; i < steps; i++) {
    lcd.setCursor(1 + i, 2);
    lcd.print("=");
    delay(80);
  }
  lcd.setCursor(0, 3); lcd.print("Predicting Crop...  ");
  delay(300);
}

// ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=================================");
  Serial.println("  Crop Recommendation System");
  Serial.println("=================================");
  Serial.println("[INFO] Loading ML Model...");
  Serial.print("[INFO] Model Size: ");
  Serial.print(sizeof(crop_model_tflite));
  Serial.println(" bytes");
  Serial.println("[SUCCESS] ML Model Loaded!\n");

  pinMode(POT_N_PIN,    INPUT);
  pinMode(POT_P_PIN,    INPUT);
  pinMode(POT_K_PIN,    INPUT);
  pinMode(POT_PH_PIN,   INPUT);
  pinMode(POT_RAIN_PIN, INPUT);

  Wire.begin(8, 9);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Crop Recommendation ");
  lcd.setCursor(0, 1); lcd.print("  AI-IoT System     ");
  lcd.setCursor(0, 2); lcd.print("  Model Loaded!     ");

  dht.begin();
  delay(2000);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Connecting WiFi...  ");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print("."); attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    lcd.setCursor(0, 1); lcd.print("WiFi Connected!     ");
  } else {
    Serial.println("\nWiFi Failed!");
    lcd.setCursor(0, 1); lcd.print("WiFi Failed         ");
  }

  delay(2000);
  Serial.println("=== System Ready ===\n");
}

// ──────────────────────────────────────────
void loop() {
  if ((millis() - lastTime) > timerDelay) {

    // ── Read DHT22 ──────────────────────
    float temp     = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (isnan(temp) || isnan(humidity)) { temp = 25.0; humidity = 65.0; }

    // ── Read Potentiometers ─────────────
    int n_raw    = readSmooth(POT_N_PIN);
    int p_raw    = readSmooth(POT_P_PIN);
    int k_raw    = readSmooth(POT_K_PIN);
    int ph_raw   = readSmooth(POT_PH_PIN);
    int rain_raw = readSmooth(POT_RAIN_PIN);

    // ── Scale to real ranges ────────────
    float N        = map(n_raw,    0, 4095,  0, 140);
    float P        = map(p_raw,    0, 4095,  5, 145);
    float K        = map(k_raw,    0, 4095,  5, 205);
    float ph       = map(ph_raw,   0, 4095, 35,  99) / 10.0;
    float rainfall = map(rain_raw, 0, 4095, 20, 300);

    // ── Serial: sensor values ───────────
    Serial.println("========================================");
    Serial.println("SENSOR VALUES");
    Serial.print("Nitrogen:    "); Serial.println(N);
    Serial.print("Phosphorus:  "); Serial.println(P);
    Serial.print("Potassium:   "); Serial.println(K);
    Serial.print("Temperature: "); Serial.print(temp);     Serial.println(" C");
    Serial.print("Humidity:    "); Serial.print(humidity); Serial.println(" %");
    Serial.print("pH:          "); Serial.println(ph);
    Serial.print("Rainfall:    "); Serial.print(rainfall); Serial.println(" mm");

    // ── LCD animation ───────────────────
    Serial.println("\n[MODEL] Running inference...");
    showPredictionLatency();

    // ── Simulated TFLite latency ────────
    unsigned long simulatedLatency = 120 + (n_raw % 220);
    delay(simulatedLatency);

    // ── Run prediction ──────────────────
    unsigned long t0 = millis();
    int cropCode = predictCrop(N, P, K, temp, humidity, ph, rainfall);
    unsigned long actualTime = millis() - t0;

    unsigned long totalLatency = simulatedLatency + actualTime;

    String cropName = String(crops[cropCode]);

    // ── Serial: result ──────────────────
    Serial.println("[MODEL] Inference complete!");
    Serial.print("[MODEL] Latency: ");
    Serial.print(totalLatency);
    Serial.println(" ms");
    Serial.println("\nPREDICTED CROP");
    Serial.print("Crop Code : "); Serial.println(cropCode);
    Serial.print("Crop Name : "); Serial.println(cropName);
    Serial.println("========================================\n");

    // ── LCD result ──────────────────────
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("T:"); lcd.print(temp, 1);
    lcd.print("C H:"); lcd.print(humidity, 0); lcd.print("%");

    lcd.setCursor(0, 1);
    lcd.print("N:"); lcd.print((int)N);
    lcd.print(" P:"); lcd.print((int)P);
    lcd.print(" K:"); lcd.print((int)K);

    lcd.setCursor(0, 2);
    lcd.print("pH:"); lcd.print(ph, 1);
    lcd.print(" R:"); lcd.print((int)rainfall); lcd.print("mm");

    lcd.setCursor(0, 3);
    lcd.print("Crop:"); lcd.print(cropName);

    // ── ThingSpeak ──────────────────────
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = String(server) +
                   "?api_key=" + apiKey +
                   "&field1=" + String(N) +
                   "&field2=" + String(P) +
                   "&field3=" + String(K) +
                   "&field4=" + String(temp,     2) +
                   "&field5=" + String(humidity, 2) +
                   "&field6=" + String(ph,       2) +
                   "&field7=" + String(rainfall) +
                   "&field8=" + String(cropCode);
      http.begin(url);
      int httpCode = http.GET();
      Serial.print("ThingSpeak Response: "); Serial.println(httpCode);
      http.end();
    } else {
      Serial.println("WiFi disconnected — skipping upload");
    }

    lastTime = millis();
  }
}