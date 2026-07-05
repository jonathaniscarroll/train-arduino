# train-arduino

A model train controller that reads a physical potentiometer via ESP32 and mirrors the result to a Unity scene in real time over WebSocket.

## Architecture

```
Potentiometer → ESP32 (WebSocket server) → WiFi → Unity (WebSocket client)
```

A debug web page is also served by the ESP32 at its local IP so you can verify values before opening Unity.

## Wiring

- Potentiometer outer legs: **3.3V** and **GND**
- Potentiometer middle (wiper) leg: **GPIO34** on the ESP32
- Do NOT use 5V — the ESP32 ADC is 3.3V only

## ESP32 Setup

1. Open `esp32/train_pot_ws/train_pot_ws.ino` in Arduino IDE
2. Set your WiFi SSID and password in the sketch
3. Install required libraries:
   - `ESPAsyncWebServer` ([ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer))
   - `AsyncTCP`
4. Upload to your ESP32
5. Open Serial Monitor (115200 baud) — the ESP32 will print its local IP
6. Visit that IP in a browser to see live pot values

## Unity Setup

1. Import `NativeWebSocket` into your Unity project:
   - Via UPM: `https://github.com/endel/NativeWebSocket.git#upm`
   - Or clone/copy the package manually
2. Copy `unity/TrainPotReceiver.cs` into your Unity Assets folder
3. Attach `TrainPotReceiver` to your train GameObject
4. In the Inspector:
   - Set **Esp32 Ip** to the IP shown in Serial Monitor
   - Assign your **Train** transform
   - Set **Start Pos** and **End Pos** to define the track range
   - Enable **Invert** if direction is reversed
5. Press Play — turn the pot and the train follows

## Calibration Tips

- If values jitter, increase the deadband (`abs(raw - lastSent) < 8`) in the sketch
- If motion feels too sensitive, adjust the `Lerp` speed (`Time.deltaTime * 8f`) in the C# script
- Raw ADC range is 0–4095 on the ESP32
