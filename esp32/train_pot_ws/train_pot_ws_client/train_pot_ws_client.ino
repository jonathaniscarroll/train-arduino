/*
 * train_pot_ws_client.ino
 * Reads a 10k pot from a ~19V train controller via a resistor divider,
 * streams raw + filtered ADC values over WebSocket to the camera ESP32.
 *
 * Uses ArduinoWebsockets (WebsocketsClient) as a client to connect to:
 *   ws://<CAMERA_IP>/ws
 *
 * HARDWARE NOTES (see esp32/WIRING.md for full diagram):
 *  - Controller pot is unmarked, measures ~10kΩ outer-to-outer
 *  - Controller reference voltage is ~19V — pot wiper must NOT go
 *    directly to ESP32. Use the voltage divider below.
 *  - Divider: wiper → R1(47kΩ) → GPIO34 node → R2(10kΩ) → GND
 *  - Gives ~3.2V max at GPIO34 when wiper is at full 19V reference
 *  - ESP32 GND must share the same ground as the controller
 *  - Add a 0.1µF ceramic cap from GPIO34 to GND (close to the pin)
 *    to reduce high-frequency ADC noise
 *  - Use ADC1 pins only (GPIO32-39) while Wi-Fi is active
 */

#include <WiFi.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

const char* ssid     = "Wireless-N";
const char* password = "";

// Camera WebSocket server (the camera's AsyncWebSocket at /ws on port 81)
const char* CAMERA_HOST = "192.168.10.100";  // camera IP
const uint16_t CAMERA_PORT = 81;
const char* CAMERA_PATH = "/ws";

const int potPin = 34;  // ADC1 — safe with Wi-Fi active

// ── Timing constants ────────────────────────────────────────────────────────
const unsigned long SEND_INTERVAL_MS    =   50;  // max send rate
const unsigned long SAMPLE_INTERVAL_US  =  200;  // gap between ADC samples
const unsigned long RECONNECT_DELAY_MS  = 3000;  // delay before reconnect

// ── State ────────────────────────────────────────────────────────────────────
int lastSent = -1;
unsigned long lastSendTime = 0;
float filtered = 0.0f;
const float alpha = 0.03f;

unsigned long lastReconnectAttempt = 0;
bool wsConnected = false;

WebsocketsClient wsClient;

// ── ADC sampling ─────────────────────────────────────────────────────────────
int readPotAveraged() {
  // Dummy read to discharge sample-and-hold capacitor
  analogRead(potPin);

  long total = 0;
  const int SAMPLES = 16;
  for (int i = 0; i < SAMPLES; i++) {
    unsigned long t = micros();
    while (micros() - t < SAMPLE_INTERVAL_US) {}  // busy-wait only 200 µs
    total += analogRead(potPin);
  }
  return (int)(total / SAMPLES);
}

// ── WebSocket callbacks ──────────────────────────────────────────────────────
void onWsMessage(WebsocketsMessage message) {
  // Optional: handle messages from camera if needed
  // String data = message.data();
}

void onWsEvent(WebsocketsEvent event, String data) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("[WS] Connected to camera");
    wsConnected = true;
    // Send initial reading
    int raw = readPotAveraged();
    filtered = (float)raw;
    wsClient.send(String(raw) + "," + String(raw));
    lastSent = (int)(filtered + 0.5f);
    lastSendTime = millis();
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("[WS] Disconnected from camera");
    wsConnected = false;
  } else if (event == WebsocketsEvent::GotPing) {
    // Optional
  } else if (event == WebsocketsEvent::GotPong) {
    // Optional
  }
}

// ── Connect to camera WebSocket ─────────────────────────────────────────────
void connectToCamera() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (wsConnected) return;

  unsigned long now = millis();
  if (now - lastReconnectAttempt < RECONNECT_DELAY_MS) return;
  lastReconnectAttempt = now;

  Serial.printf("[WS] Connecting to ws://%s:%u%s\n",
                CAMERA_HOST, CAMERA_PORT, CAMERA_PATH);

  wsClient.onMessage(onWsMessage);
  wsClient.onEvent(onWsEvent);

  bool ok = wsClient.connect(CAMERA_HOST, CAMERA_PORT, CAMERA_PATH);
  if (!ok) {
    Serial.println("[WS] Connection failed");
    wsConnected = false;
  }
}

// ── Notify camera with pot value ────────────────────────────────────────────
void notifyCamera() {
  if (!wsConnected) return;

  int raw = readPotAveraged();
  filtered = filtered + alpha * (raw - filtered);
  int filt = (int)(filtered + 0.5f);

  // Deadband: skip send if change is tiny AND we sent recently
  if (abs(filt - lastSent) < 12 && millis() - lastSendTime < SEND_INTERVAL_MS) return;

  lastSent     = filt;
  lastSendTime = millis();
  wsClient.send(String(raw) + "," + String(filt));
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(potPin, INPUT);

  // Configure ADC once in setup — not in the read loop
  analogReadResolution(12);        // 0–4095
  analogSetAttenuation(ADC_11db);  // 0–3.3V input range

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("train_pot_ws_client ready");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  wsClient.poll();  // handle WebSocket events

  if (!wsConnected) {
    connectToCamera();
  } else {
    notifyCamera();
  }

  delay(10);
}