/*
 * train_pot_ws.ino
 * Reads a 10k pot from a ~19V train controller via a resistor divider,
 * streams raw + filtered ADC values over WebSocket.
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
 *
 * WEBSOCKET STABILITY NOTES:
 *  - A ping frame is sent every PING_INTERVAL_MS to prevent Unity
 *    NativeWebSocket from closing the connection with "abnormal closure".
 *    Without keepalive pings the client-side TCP idle timeout fires.
 *  - readPotAveraged() uses millis()-based timing instead of
 *    delayMicroseconds() so the async TCP stack is never blocked.
 *  - cleanupClients() is rate-limited to avoid kicking clients that
 *    are still completing their opening handshake.
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* ssid     = "Wireless-N";
const char* password = "";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const int potPin = 34;            // ADC1 — safe with Wi-Fi active

// ── Timing constants ────────────────────────────────────────────────────────
const unsigned long PING_INTERVAL_MS    = 5000;  // send WS ping every 5 s
const unsigned long CLEANUP_INTERVAL_MS = 2000;  // cleanupClients every 2 s
const unsigned long SEND_INTERVAL_MS    =   50;  // max send rate
const unsigned long SAMPLE_INTERVAL_US  =  200;  // gap between ADC samples

// ── State ────────────────────────────────────────────────────────────────────
int lastSent = -1;
unsigned long lastSendTime    = 0;
unsigned long lastPingTime    = 0;
unsigned long lastCleanupTime = 0;
float filtered = 0.0f;
const float alpha = 0.03f;

// ── ADC sampling ─────────────────────────────────────────────────────────────
// Uses millis/micros-based non-blocking loop rather than delayMicroseconds()
// so the async TCP stack is never starved.
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

// ── HTML debug page ──────────────────────────────────────────────────────────
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Train Pot</title>
  <style>
    body { font-family: sans-serif; padding: 20px; background: #111; color: #eee; }
    .value { font-size: 2rem; font-weight: bold; color: #4af; }
    .label { margin-top: 12px; opacity: 0.8; }
    #bar { height: 20px; background: #4af; width: 0%; transition: width 0.1s; border-radius: 4px; margin-top: 12px; }
    #status { margin-top: 8px; font-size: 0.85rem; opacity: 0.6; }
  </style>
</head>
<body>
  <h1>Train Controller</h1>
  <div class="label">Raw ADC: <span class="value" id="raw">0</span> / 4095</div>
  <div class="label">Filtered: <span class="value" id="filtered">0</span> / 4095</div>
  <div style="background:#333; border-radius:4px; overflow:hidden; margin-top: 12px;">
    <div id="bar"></div>
  </div>
  <div id="status">Connecting...</div>
  <script>
    const rawEl      = document.getElementById('raw');
    const filteredEl = document.getElementById('filtered');
    const bar        = document.getElementById('bar');
    const statusEl   = document.getElementById('status');
    function connect() {
      const ws = new WebSocket(`ws://${location.host}/ws`);
      ws.onopen    = ()  => { statusEl.textContent = 'Connected'; };
      ws.onclose   = ()  => { statusEl.textContent = 'Disconnected — reconnecting...'; setTimeout(connect, 2000); };
      ws.onerror   = ()  => { statusEl.textContent = 'Error'; };
      ws.onmessage = e  => {
        // ignore server ping frames (empty or "ping")
        if (!e.data || e.data === 'ping') return;
        const [raw, filtered] = e.data.split(',');
        rawEl.textContent      = raw;
        filteredEl.textContent = filtered;
        bar.style.width        = (parseInt(filtered) / 4095 * 100).toFixed(1) + '%';
      };
    }
    connect();
  </script>
</body>
</html>
)rawliteral";

// ── WebSocket event handler ───────────────────────────────────────────────────
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected from %s\n",
                  client->id(), client->remoteIP().toString().c_str());
    // Send current reading immediately on connect
    int raw = readPotAveraged();
    filtered = (float)raw;
    client->text(String(raw) + "," + String(raw));
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_ERROR) {
    Serial.printf("[WS] Client #%u error\n", client->id());
  }
}

// ── Notify clients with pot value ─────────────────────────────────────────────
void notifyClients() {
  if (ws.count() == 0) return;  // no clients — skip ADC read entirely

  int raw = readPotAveraged();
  filtered = filtered + alpha * (raw - filtered);
  int filt = (int)(filtered + 0.5f);

  // Deadband: skip send if change is tiny AND we sent recently
  if (abs(filt - lastSent) < 12 && millis() - lastSendTime < SEND_INTERVAL_MS) return;

  lastSent     = filt;
  lastSendTime = millis();
  ws.textAll(String(raw) + "," + String(filt));
}

// ── Keepalive ping ────────────────────────────────────────────────────────────
// Prevents Unity NativeWebSocket from dropping the connection with
// "WebSocket closed: abnormal" due to TCP idle timeout.
void sendPing() {
  if (ws.count() == 0) return;
  unsigned long now = millis();
  if (now - lastPingTime < PING_INTERVAL_MS) return;
  lastPingTime = now;
  ws.pingAll();  // sends a WebSocket ping frame to all connected clients
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

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.begin();
  Serial.println("WebSocket server started — visit http://" + WiFi.localIP().toString());
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  notifyClients();
  sendPing();

  // Rate-limit cleanupClients — calling it every loop iteration can
  // kick clients that are still completing the opening handshake.
  unsigned long now = millis();
  if (now - lastCleanupTime >= CLEANUP_INTERVAL_MS) {
    lastCleanupTime = now;
    ws.cleanupClients();
  }

  delay(10);
}
