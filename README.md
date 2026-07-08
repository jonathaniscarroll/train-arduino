# train-arduino

A physical-to-digital model railway installation. A potentiometer on a real model train throttle is read by an ESP32 and streamed over WebSocket to Unity, where it drives a virtual locomotive via the **WSM Train Controller (Railroad System) v3.4** asset — so the Unity train mirrors the physical train's speed in real time.

## System Architecture

```
Physical throttle pot
        │
        ▼
  ESP32 ADC (GPIO34)
  EMA filter applied
        │  WebSocket  ws://<esp32-ip>/ws
        ▼
  Unity NativeWebSocket client
        │
        ▼
  TrainSpeedReceiver.cs
  parses "raw,filtered" message
  maps filtered ADC → KPH
        │  ILocomotive API (WSM Train Controller v3.4)
        ▼
  locomotive.MaxSpeed = targetKph
  locomotive.Acceleration = 1 (forward)
  locomotive.Brake = 0 / 1
```

A debug web page is also served by the ESP32 at its local IP — visit it in any browser to verify raw and filtered pot values before opening Unity.

## Repository Structure

```
train-arduino/
├── esp32/
│   └── train_pot_ws/
│       └── train_pot_ws.ino        # ESP32 firmware: reads pot, EMA filter, WebSocket server
├── unity/
│   └── TrainSpeedReceiver.cs       # Unity script: WebSocket client → ILocomotive API
├── Train Controller (Railroad System) User Manual v3.4.pdf
└── README.md
```

## Wiring

| Pot leg | Connect to |
|---------|-----------|
| Outer leg 1 | **3.3V** on ESP32 |
| Outer leg 2 | **GND** on ESP32 |
| Middle (wiper) | **GPIO34** on ESP32 |

> ⚠️ Use **3.3V only** — the ESP32 ADC is not 5V tolerant.

## ESP32 Setup

1. Open `esp32/train_pot_ws/train_pot_ws.ino` in Arduino IDE
2. Set your WiFi SSID and password at the top of the sketch
3. Install required Arduino libraries:
   - [`ESPAsyncWebServer`](https://github.com/ESP32Async/ESPAsyncWebServer)
   - `AsyncTCP`
4. Upload to the ESP32
5. Open Serial Monitor at **115200 baud** — the local IP will be printed on connect
6. Visit that IP in a browser to confirm live pot readings

### Message Format

The ESP32 broadcasts a WebSocket message on every ADC read:

```
raw,filtered
```

For example: `2051,2048`

- **raw** — direct 12-bit ADC reading (0–4095)
- **filtered** — EMA-smoothed value (reduces jitter); this is what `TrainSpeedReceiver.cs` uses

## Unity Setup

### Requirements

- **WSM Train Controller (Railroad System) v3.4** — available on the Unity Asset Store.  
  Both `TrainController_v3` (physics-based) and `SplineBasedLocomotive` (spline-based) are supported; the script targets the shared `ILocomotive` interface.
- **NativeWebSocket** — install via UPM:
  ```
  https://github.com/endel/NativeWebSocket.git#upm
  ```

### Steps

1. Import NativeWebSocket into your project via the Package Manager
2. Copy `unity/TrainSpeedReceiver.cs` into your Unity Assets folder
3. Attach `TrainSpeedReceiver` to any GameObject in your scene
4. In the Inspector:

| Field | Description |
|-------|-------------|
| **Esp32 Ip** | IP address printed by ESP32 on Serial Monitor |
| **Locomotive Object** | Your locomotive's GameObject (must have `TrainController_v3` or `SplineBasedLocomotive`) |
| **Max Speed Kph** | Unity train's top speed at full pot deflection. Keep ≤ 105 for physics-based; unlimited for spline-based. |
| **Invert Speed** | Tick if turning the pot up slows the Unity train instead of speeding it up |
| **Speed Smoothing** | `0.001–1` — lower = snappier response, higher = gradual locomotive-style acceleration |
| **Deadband** | Normalised ADC threshold below which the train is treated as fully stopped (prevents jitter creep) |

5. Press **Play** — the Unity locomotive now mirrors the physical train's throttle position

### How It Works

`TrainSpeedReceiver.cs` connects to the ESP32's WebSocket server, parses the `"raw,filtered"` message, and writes to the locomotive via the WSM `ILocomotive` interface (§11.1 of the manual):

```csharp
_loco.EnginesOn    = true;
_loco.Acceleration = 1f;          // always driving forward
_loco.MaxSpeed     = normalisedPot * maxSpeedKph;  // throttle
_loco.Brake        = (speed == 0) ? 1f : 0f;
```

The asset's own acceleration physics, wagon coupling, SFX, Control Zones, and signals all continue to work normally — this script only feeds the throttle value in from the real-world pot.

## Calibration

| Symptom | Fix |
|---------|-----|
| Unity train speed is jittery | Increase **Deadband** in Inspector |
| Unity train accelerates/decelerates too instantly | Increase **Speed Smoothing** |
| Unity train moves opposite to physical | Enable **Invert Speed** |
| No connection | Confirm IP in Serial Monitor; check WiFi; verify ESP32 upload succeeded |
| `ILocomotive` not found | Ensure locomotive GameObject has `TrainController_v3` or `SplineBasedLocomotive` component |

## Dependencies

| Dependency | Where |
|-----------|-------|
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | Arduino IDE Library Manager |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | Arduino IDE Library Manager |
| [NativeWebSocket](https://github.com/endel/NativeWebSocket) | Unity Package Manager (UPM) |
| [WSM Train Controller (Railroad System) v3.4](https://assetstore.unity.com/publishers/16163) | Unity Asset Store |
