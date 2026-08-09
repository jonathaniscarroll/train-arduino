#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

// ===========================
// Tunable settings
// ===========================
const char *ssid = "Wireless-N";
const char *password = "";

const char *controllerHost = "192.168.10.101";
const uint16_t controllerPort = 80;
const char *controllerPath = "/ws";

#define HOSTNAME "traincam"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define MATRIX_DATA_PIN D10
#define MATRIX_CLK_PIN  D8
#define MATRIX_CS_PIN   D9

const char *SCROLL_TEXT = "EVERY POSSIBLE WORLD IS AS REAL AS OUR ACTUAL WORLD    ";
const uint8_t MATRIX_INTENSITY = 2;

// Map controller filtered ADC to Parola scroll speed.
// Lower Parola speed number = faster movement.
const int ADC_MIN_ACTIVE = 900;
const int ADC_MAX_ACTIVE = 3200;
const int PAROLA_SPEED_SLOW = -120;
const int PAROLA_SPEED_FAST = -12;
const int STOP_THRESHOLD = 850;
const bool HOLD_TEXT_WHEN_STOPPED = false;

const uint32_t WS_RECONNECT_MS = 3000;
const uint32_t STATUS_LOG_MS = 2000;

// ===========================
// Camera board config
// ===========================
#include "board_config.h"

MD_Parola matrix = MD_Parola(HARDWARE_TYPE, MATRIX_DATA_PIN, MATRIX_CLK_PIN, MATRIX_CS_PIN, MAX_DEVICES);
WebServer server(80);
WebsocketsClient wsClient;

bool wsConnected = false;
unsigned long lastWsAttempt = 0;
unsigned long lastStatusLog = 0;
int latestRaw = 0;
int latestFiltered = 0;
int currentMatrixSpeed = PAROLA_SPEED_SLOW;
bool matrixStopped = false;

void startCameraServer();
void setupLedFlash();

String htmlPage() {
  String html;
  html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>XIAO Train Camera</title>";
  html += "<style>body{font-family:Arial,sans-serif;background:#111;color:#eee;margin:0;padding:20px}"
          ".card{background:#1b1b1b;border:1px solid #333;border-radius:12px;padding:16px;max-width:920px}"
          "img{width:100%;max-width:800px;height:auto;border-radius:12px;border:1px solid #333}"
          "code{background:#222;padding:2px 6px;border-radius:6px} .ok{color:#7ee787}.warn{color:#ffd866}</style></head><body>";
  html += "<div class='card'><h1>XIAO Train Camera</h1>";
  html += "<p>Camera stream: <code>/stream</code> &nbsp; Snapshot: <code>/snapshot</code> &nbsp; Status: <code>/status</code></p>";
  html += "<p>Controller WS: ";
  html += wsConnected ? "<span class='ok'>connected</span>" : "<span class='warn'>disconnected</span>";
  html += " &nbsp; Raw: " + String(latestRaw) + " &nbsp; Filtered: " + String(latestFiltered) + " &nbsp; Matrix speed: " + String(currentMatrixSpeed) + "</p>";
  html += "<img src='/stream' alt='Train camera stream'>";
  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleStatus() {
  String json = "{";
  json += "\"wsConnected\":" + String(wsConnected ? "true" : "false");
  json += ",\"raw\":" + String(latestRaw);
  json += ",\"filtered\":" + String(latestFiltered);
  json += ",\"matrixSpeed\":" + String(currentMatrixSpeed);
  json += ",\"matrixStopped\":" + String(matrixStopped ? "true" : "false");
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleSnapshot() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(503, "text/plain", "Camera frame unavailable");
    return;
  }
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Length", String(fb->len));
  server.send(200);
  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleStream() {
  WiFiClient client = server.client();
  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: multipart/x-mixed-replace; boundary=frame\r\n");
  client.print("Cache-Control: no-cache\r\n\r\n");

  while (client.connected()) {
    wsClient.poll();

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      delay(30);
      continue;
    }

    client.print("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ");
    client.print(fb->len);
    client.print("\r\n\r\n");
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);

    if (matrixStopped) {
      delay(60);
    } else {
      delay(30);
    }
  }
}

void updateMatrixSpeedFromFiltered(int filtered) {
  latestFiltered = filtered;

  if (filtered <= STOP_THRESHOLD) {
    matrixStopped = true;
    currentMatrixSpeed = PAROLA_SPEED_SLOW;
    matrix.setSpeed(currentMatrixSpeed);
    return;
  }

  matrixStopped = false;
  int clamped = constrain(filtered, ADC_MIN_ACTIVE, ADC_MAX_ACTIVE);
  currentMatrixSpeed = map(clamped, ADC_MIN_ACTIVE, ADC_MAX_ACTIVE, PAROLA_SPEED_SLOW, PAROLA_SPEED_FAST);
  currentMatrixSpeed = constrain(currentMatrixSpeed, PAROLA_SPEED_FAST, PAROLA_SPEED_SLOW);
  matrix.setSpeed(currentMatrixSpeed);
}

void onWsMessage(WebsocketsMessage message) {
  String data = message.data();
  if (data.length() == 0 || data == "ping") return;

  int comma = data.indexOf(',');
  if (comma <= 0) return;

  latestRaw = data.substring(0, comma).toInt();
  int filtered = data.substring(comma + 1).toInt();
  updateMatrixSpeedFromFiltered(filtered);
}

void connectControllerWs() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (wsConnected) return;
  if (millis() - lastWsAttempt < WS_RECONNECT_MS) return;

  lastWsAttempt = millis();
  Serial.printf("Connecting WS to ws://%s:%u%s\n", controllerHost, controllerPort, controllerPath);

  wsClient.onMessage(onWsMessage);
  wsClient.onEvent([](WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
      wsConnected = true;
      Serial.println("Controller WebSocket connected");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
      wsConnected = false;
      Serial.println("Controller WebSocket disconnected");
    } else if (event == WebsocketsEvent::GotPing) {
      wsConnected = true;
    } else if (event == WebsocketsEvent::GotPong) {
      wsConnected = true;
    }
  });

  bool ok = wsClient.connect(controllerHost, controllerPort, controllerPath);
  wsConnected = ok;
  if (!ok) Serial.println("Controller WebSocket connect failed");
}

void setupMatrix() {
  matrix.begin();
  matrix.setIntensity(MATRIX_INTENSITY);
  matrix.displayClear();
  matrix.setSpeed(PAROLA_SPEED_SLOW);
  matrix.displayText((char*)SCROLL_TEXT, PA_LEFT, currentMatrixSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  if (config.pixel_format == PIXFORMAT_JPEG && s) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  if (s) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
  }
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  if (s) s->set_vflip(s, 1);
#endif

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  return true;
}

void setupWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/snapshot", HTTP_GET, handleSnapshot);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
}

void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(HOSTNAME)) {
    Serial.printf("mDNS started: http://%s.local/\n", HOSTNAME);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  setupMatrix();

  if (!initCamera()) {
    return;
  }

  setupWifi();
  setupWeb();
  connectControllerWs();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wsConnected) connectControllerWs();
    wsClient.poll();
  }

  if (matrixStopped) {
    if (!HOLD_TEXT_WHEN_STOPPED) {
      if (matrix.displayAnimate()) matrix.displayReset();
    }
  } else {
    if (matrix.displayAnimate()) matrix.displayReset();
  }

  if (millis() - lastStatusLog >= STATUS_LOG_MS) {
    lastStatusLog = millis();
    Serial.printf("WS=%s raw=%d filtered=%d matrixSpeed=%d ip=%s\n",
                  wsConnected ? "ON" : "OFF",
                  latestRaw,
                  latestFiltered,
                  currentMatrixSpeed,
                  WiFi.localIP().toString().c_str());
  }

  delay(5);
}