#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* ssid     = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const int potPin = 34;
int lastSent = -1;
unsigned long lastSendTime = 0;

// Debug web page — visit the ESP32's IP in a browser to see live values
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Train Pot</title>
  <style>
    body { font-family: sans-serif; padding: 20px; background: #111; color: #eee; }
    #raw { font-size: 3rem; font-weight: bold; color: #4af; }
    #bar { height: 20px; background: #4af; width: 0%; transition: width 0.1s; border-radius: 4px; margin-top: 12px; }
  </style>
</head>
<body>
  <h1>Train Controller</h1>
  <p>Raw ADC: <span id="raw">0</span> / 4095</p>
  <div style="background:#333; border-radius:4px; overflow:hidden;">
    <div id="bar"></div>
  </div>
  <script>
    const raw = document.getElementById('raw');
    const bar = document.getElementById('bar');
    const ws  = new WebSocket(`ws://${location.host}/ws`);
    ws.onmessage = e => {
      raw.textContent = e.data;
      bar.style.width = (parseInt(e.data) / 4095 * 100).toFixed(1) + '%';
    };
  </script>
</body>
</html>
)rawliteral";

void notifyClients() {
  int raw = analogRead(potPin);
  // Only broadcast if value changed meaningfully or 30ms have passed
  if (abs(raw - lastSent) < 8 && millis() - lastSendTime < 30) return;
  lastSent = raw;
  lastSendTime = millis();
  ws.textAll(String(raw));
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    // Send current value immediately on new connection
    client->text(String(analogRead(potPin)));
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
