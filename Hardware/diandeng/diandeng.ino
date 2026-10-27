/*
 * ESP32-S3-N16R8 ADC Monitor with WiFi TCP upload
 *
 * Reads voltage-divider module on GPIO4 (ADC1_CH3), computes sensor resistance,
 * sends JSON data over TCP to Qt上位机, and prints to Serial for debugging.
 *
 * Module circuit:
 *   VCC(3V3) -> [R_REF=9.92M internal] -> AO -> [ext pin1] -> [sensor] -> [ext pin2] -> GND
 *   R_sensor = R_REF * V_AO / (VCC - V_AO)
 */

#include <WiFi.h>
#include "breath_ae.h"
#include "breath_model_config.h"

// ===================== WiFi & TCP Config =====================

const char *WIFI_SSID = "飘逸花的vivo S18 Pro";
const char *WIFI_PASS = "00000000";

//const char *WIFI_SSID = "Xiaomi 14";
//const char *WIFI_PASS = "66668888";


const int   TCP_PORT = 8080;
const unsigned long TCP_RECONNECT_MS = 3000;  // wait between reconnect attempts

// ===================== TCP Targets =====================
// ESP32 tries each IP in order; the first one to accept wins.
// Add/remove/comment lines as needed. No code changes elsewhere.
const char *TCP_TARGETS[] = {
  "192.168.224.7",    // PC (Qt上位机 on Windows)
   "gateway",      // phone (Qt上位机 on Android) — uncomment when needed
};
const int TCP_TARGET_COUNT = sizeof(TCP_TARGETS) / sizeof(TCP_TARGETS[0]);

// ===================== ADC Config =====================

const int ADC_PIN = 4;                // GPIO4 = H1-4 = ADC1_CH3
const int VCC_MV = 3300;              // supply voltage (mV)
const int ADC_MAX = 4095;             // 12-bit ADC
const int SAMPLE_COUNT = 8;           // oversampling
const int SAMPLE_INTERVAL_MS = 200;   // upload period (5 Hz)

// Reference resistor: VCC--R_REF--AO--sensor--GND
const double R_REF_OHM = 120.0;        // module VCC-AO resistor

// ===================== Globals =====================

WiFiClient tcpClient;
unsigned long lastSampleMs = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long seq = 0;
unsigned long errorCount = 0;
bool wifiConnected = false;
bool tcpConnected = false;
int tcpTargetIndex = 0;   // current target IP index being tried (round-robin)

// ===================== Helpers =====================

int readAdcAvg(int pin, int samples) {
  long sum = 0;
  int valid = 0;
  for (int i = 0; i < samples; i++) {
    int v = analogRead(pin);
    if (v >= 0) { sum += v; valid++; }
  }
  if (valid == 0) return -1;
  return (int)(sum / valid);
}

double adcToMv(int adc) {
  if (adc <= 0) return 0.0;
  if (adc >= ADC_MAX) return (double)VCC_MV;
  return (double)adc * VCC_MV / ADC_MAX;
}

double computeResistanceOhm(double voltageMv) {
  if (voltageMv <= 0.0) return -1.0;
  double vcc = (double)VCC_MV;
  if (voltageMv >= vcc) return -2.0;
  // R_REF is upper leg: R_sensor = R_REF * V / (VCC - V)
  return R_REF_OHM * voltageMv / (vcc - voltageMv);
}

// ===================== WiFi =====================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return;
  }
  wifiConnected = false;

  Serial.print("WiFi connecting to ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (millis() - start < 10000) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println();
      Serial.print("WiFi connected. IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
    delay(300);
    Serial.print(".");
  }
  Serial.println(" FAILED");
}

// ===================== TCP =====================

/*
 * Resolve one target entry to an IPAddress.
 *   "192.168.13.7" → literal IP
 *   "gateway"      → WiFi.gatewayIP()
 */
bool resolveTarget(const char *target, IPAddress &ip) {
  if (strcmp(target, "gateway") == 0) {
    ip = WiFi.gatewayIP();
    return true;
  }
  return ip.fromString(target);
}

bool ensureTcpConnected() {
  if (tcpClient.connected()) {
    tcpConnected = true;
    return true;
  }

  tcpConnected = false;
  if (!wifiConnected) return false;

  unsigned long now = millis();
  if (now - lastReconnectAttempt < TCP_RECONNECT_MS) return false;
  lastReconnectAttempt = now;

  // Try the current target; if it fails, advance to next for next cycle
  const char *target = TCP_TARGETS[tcpTargetIndex];
  IPAddress targetIp;
  if (!resolveTarget(target, targetIp)) {
    Serial.print("TCP bad target: ");
    Serial.println(target);
    tcpTargetIndex = (tcpTargetIndex + 1) % TCP_TARGET_COUNT;
    return false;
  }

  Serial.print("TCP connecting to ");
  Serial.print(targetIp);
  Serial.print(" [");
  Serial.print(target);
  Serial.print("]:");
  Serial.print(TCP_PORT);
  Serial.print(" ... ");

  tcpClient.stop();  // reset any stale state
  if (tcpClient.connect(targetIp, TCP_PORT, 3000)) {
    tcpConnected = true;
    Serial.println("OK");
    return true;
  }
  Serial.println("FAILED");
  // Rotate to next target so we try each in turn
  tcpTargetIndex = (tcpTargetIndex + 1) % TCP_TARGET_COUNT;
  return false;
}

// ===================== Breath AI Globals =====================

static float breathWindow[BREATH_WINDOW_SIZE];  // sliding ring buffer
static int breathIdx = 0;                       // write position
static float breathAnomalyScore = 0.0f;         // latest anomaly score
static const char *breathStatus = "";           // "正常" or "异常"

// Helper: rotate ring buffer so most recent data is at the end
static void breathRotateWindow(float *buf, int writePos) {
  float temp[BREATH_WINDOW_SIZE];
  int start = writePos;  // oldest entry is right after writePos
  for (int i = 0; i < BREATH_WINDOW_SIZE; i++) {
    temp[i] = buf[(start + i) % BREATH_WINDOW_SIZE];
  }
  memcpy(buf, temp, sizeof(temp));
}

void sendJsonOverTcp(unsigned long ts, int adc, double voltageMv, double resistanceOhm) {
  if (!tcpConnected) return;

  // Build JSON line per ESP32_DATA_PROTOCOL.md
  String json = "{\"timestamp\":";
  json += ts;
  json += ",\"adc\":";
  json += adc;
  json += ",\"voltageMv\":";
  json += String(voltageMv, 1);
  if (resistanceOhm >= 0.0) {
    json += ",\"resistanceOhm\":";
    json += String(resistanceOhm, 1);
  }
  // Edge-AI breath analysis fields
  if (breathStatus[0] != '\0') {
    json += ",\"breathStatus\":\"";
    json += breathStatus;
    json += "\"";
    json += ",\"anomalyScore\":";
    json += String(breathAnomalyScore, 3);
  }
  json += "}\n";

  tcpClient.print(json);
}

// ===================== Setup =====================

void setup() {
  Serial.begin(115200);
  delay(100);

  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);

  Serial.println();
  Serial.println("============================================");
  Serial.println(" ESP32-S3-N16R8 ADC + WiFi TCP Monitor");
  Serial.println("============================================");
  Serial.print(" ADC Pin    : GPIO");
  Serial.println(ADC_PIN);
  Serial.print(" R_REF (ohm): ");
  Serial.println(R_REF_OHM, 0);
  Serial.print(" Sample int : ");
  Serial.print(SAMPLE_INTERVAL_MS);
  Serial.println(" ms");
  Serial.print(" TCP port   : ");
  Serial.println(TCP_PORT);
  Serial.print(" TCP targets : ");
  for (int i = 0; i < TCP_TARGET_COUNT; i++) {
    if (i > 0) Serial.print(", ");
    Serial.print(TCP_TARGETS[i]);
  }
  Serial.println();
  Serial.println("--------------------------------------------");

  WiFi.mode(WIFI_STA);
  connectWiFi();

  // Init edge-AI breath detector
  if (breathAeInit()) {
    Serial.println("Breath AE: OK");
  } else {
    Serial.println("Breath AE: FAILED (no AI, DSP fallback only)");
  }
}

// ===================== Loop =====================

void loop() {
  unsigned long now = millis();

  // Maintain WiFi
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    tcpConnected = false;
    connectWiFi();
  } else {
    wifiConnected = true;
  }

  // Maintain TCP
  if (wifiConnected) {
    ensureTcpConnected();
  }

  // Check TCP health
  if (tcpConnected && !tcpClient.connected()) {
    tcpConnected = false;
    tcpTargetIndex = 0;  // re-scan from first target on reconnect
  }

  // Sample on interval
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) {
    delay(1);
    return;
  }
  lastSampleMs = now;

  int adc = readAdcAvg(ADC_PIN, SAMPLE_COUNT);
  if (adc < 0) {
    errorCount++;
    return;
  }

  double voltageMv = adcToMv(adc);
  double resistanceOhm = computeResistanceOhm(voltageMv);
  seq++;

  // --- Serial debug ---
  Serial.print("[");
  Serial.print(seq);
  Serial.print("] ADC:");
  Serial.print(adc);
  Serial.print(" V:");
  Serial.print(voltageMv, 1);
  Serial.print("mV R:");
  if (resistanceOhm < 0.0) {
    Serial.print(resistanceOhm < -1.5 ? "OPEN" : "SHORT");
  } else {
    Serial.print(resistanceOhm, 1);
    Serial.print(" ohm");
  }
  Serial.print("  TCP:");
  Serial.println(tcpConnected ? "UP" : "DOWN");

  // --- Breath AI analysis ---
  // Feed ring buffer; run inference every 30 samples (6 seconds)
  breathWindow[breathIdx % BREATH_WINDOW_SIZE] = (float)resistanceOhm;
  breathIdx++;

  if (breathIdx >= BREATH_WINDOW_SIZE && (breathIdx % 30) == 0) {
    float window[BREATH_WINDOW_SIZE];
    memcpy(window, breathWindow, sizeof(window));
    breathRotateWindow(window, breathIdx % BREATH_WINDOW_SIZE);

    // --- DSP: breathing rate ---
    float breathRate = breathDetectRate(window);

    // --- AI: anomaly score ---
    float normWindow[BREATH_WINDOW_SIZE];
    memcpy(normWindow, window, sizeof(window));
    breathNormalizeWindow(normWindow);
    breathAnomalyScore = breathAePredict(normWindow);

    // --- DSP: peak-to-peak for apnea detection ---
    float rMin = breathWindow[0], rMax = breathWindow[0];
    for (int i = 1; i < BREATH_WINDOW_SIZE; i++) {
      if (breathWindow[i] < rMin) rMin = breathWindow[i];
      if (breathWindow[i] > rMax) rMax = breathWindow[i];
    }
    float rPtp = rMax - rMin;

    // === 5-class decision ===
    if (rPtp < 0.08f) {
      breathStatus = "屏气";
    } else if (breathAnomalyScore > BREATH_ANOMALY_THRESHOLD) {
      breathStatus = "呼吸异常";
    } else if (breathRate > 0.0f && breathRate > 30.0f) {
      breathStatus = "呼吸过快";
    } else if (breathRate > 0.0f && breathRate < 6.0f) {
      breathStatus = "呼吸过慢";
    } else {
      breathStatus = "呼吸正常";
    }

    Serial.print("  Breath: ");
    Serial.print(breathStatus);
    Serial.print(" rate=");
    Serial.print(breathRate, 1);
    Serial.print("bpm score=");
    Serial.print(breathAnomalyScore, 2);
    Serial.print(" ptp=");
    Serial.print(rPtp, 2);
    Serial.println("");
  }

  // --- TCP upload ---
  sendJsonOverTcp(now, adc, voltageMv, resistanceOhm);
}
