#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* ssid     = "Wireless-N";
const char* password = "";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const int potPin = 34;
int lastSent = -1;
unsigned long lastSendTime = 0;
float filtered = 0.0f;
const float alpha = 0.08f;

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
  </style>
</head>
<body>
  <h1>Train Controller</h1>
  <div class="label">Raw ADC: <span class="value" id="raw">0</span> / 4095</div>
  <div class="label">Filtered: <span class="value" id="filtered">0</span> / 4095</div>
  <div style="background:#333; border-radius:4px; overflow:hidden; margin-top: 12px;">
    <div id="bar"></div>
  </div>
  <script>
    const rawEl = document.getElementById('raw');
    const filteredEl = document.getElementById('filtered');
    const bar = document.getElementById('bar');
    const ws  = new WebSocket(`ws://${location.host}/ws`);
    ws.onmessage = e => {
      const [raw, filtered] = e.data.split(',');
      rawEl.textContent = raw;
      filteredEl.textContent = filtered;
      bar.style.width = (parseInt(filtered) / 4095 * 100).toFixed(1) + '%';
    };
  </script>
</body>
</html>
)rawliteral";

void notifyClients() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  int raw = analogRead(potPin);
  filtered = filtered + alpha * (raw - filtered);
  int filt = (int)(filtered + 0.5f);
  if (abs(filt - lastSent) < 8 && millis() - lastSendTime < 30) return;
  lastSent = filt;
  lastSendTime = millis();
  ws.textAll(String(raw) + "," + String(filt));
  Serial.println(raw);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    int raw = analogRead(potPin);
    filtered = raw;
    client->text(String(raw) + "," + String(raw));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(potPin, INPUT);

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
  Serial.println("WebSocket server started at /ws");
}

void loop() {
  notifyClients();
  ws.cleanupClients();
  delay(10);
  
}