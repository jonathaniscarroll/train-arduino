# train-arduino

A physical-to-digital model railway installation. A potentiometer on a real model train throttle is read by an ESP32 and streamed over WebSocket to Unity, where it drives a virtual locomotive via the **WSM Train Controller (Railroad System) v3.4** asset — so the Unity train mirrors the physical train's speed in real time.

## System Architecture

```
Physical throttle pot (~19V controller, 10kΩ pot)
        │
        ▼
  Voltage divider (R1=47kΩ, R2=10kΩ) → scales ~19V down to ~3.3V
        │
        ▼
  ESP32 ADC GPIO34 (ADC1 — safe with Wi-Fi active)
  16-sample averaging + EMA filter (α=0.03) applied
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
│   ├── WIRING.md                       # Full wiring diagram, divider math, debug checklist
│   └── train_pot_ws/
│       └── train_pot_ws.ino            # ESP32 firmware: reads pot, averaging + EMA filter, WebSocket server
├── unity/
│   └── TrainSpeedReceiver.cs           # Unity script: WebSocket client → ILocomotive API
├── Train Controller (Railroad System) User Manual v3.4.pdf
└── README.md
```

## Wiring

> ⚠️ The controller runs at **~19V** — the pot wiper **must not** connect directly to the ESP32. A voltage divider is required. See [`esp32/WIRING.md`](esp32/WIRING.md) for the full diagram.

### Pot Identification

The throttle pot has no markings. It measures ~10kΩ outer-to-outer on a multimeter (reads `010` on the 2000k range). The controller's reference voltage is ~19V.

### Voltage Divider (wiper → ESP32)

```
Controller ref (~19V) ──── Pot outer pin A
Controller GND        ──── Pot outer pin B

Pot wiper ────────────────────── Controller speed input node
          │
          ├── R1 (47kΩ) ──┬── ESP32 GPIO34 (ADC1)
                          │
                        R2 (10kΩ)
                          │
Controller GND ────────────┴── ESP32 GND
```

**Divider math:** `19V × (10k ÷ (47k + 10k)) ≈ 3.33V` at full throttle — just within the ESP32's 3.3V ADC limit.

### Hardware notes

- **Shared ground is mandatory** — ESP32 GND and controller GND must be the same node
- **Add a 0.1µF ceramic cap** from GPIO34 to GND (close to the ESP32 pin) to reduce noise
- **Use ADC1 pins only** (GPIO32–39) while Wi-Fi is active — ADC2 is disabled by Wi-Fi
- **Measure the divider output** with a multimeter before connecting the ESP32, confirm it stays under 3.3V at max throttle

## ESP32 Setup

1. Open `esp32/train_pot_ws/train_pot_ws.ino` in Arduino IDE
2. Set your WiFi SSID and password at the top of the sketch
3. Install required Arduino libraries:
   - [`ESPAsyncWebServer`](https://github.com/ESP32Async/ESPAsyncWebServer)
   - `AsyncTCP`
4. Wire up the voltage divider as shown above
5. Upload to the ESP32
6. Open Serial Monitor at **115200 baud** — the local IP will be printed on connect
7. Visit that IP in a browser to confirm live pot readings

### Message Format

The ESP32 broadcasts a WebSocket message whenever the value changes meaningfully:

```
raw,filtered
```

For example: `2051,2048`

- **raw** — 16-sample averaged 12-bit ADC reading (0–4095)
- **filtered** — EMA-smoothed value (α=0.03); this is what `TrainSpeedReceiver.cs` uses

### Noise Reduction

ESP32 ADC is inherently noisy, especially when Wi-Fi is active. Four layers of mitigation are used:

| Layer | What it does |
|---|---|
| 0.1µF cap on GPIO34 | Filters high-frequency hardware noise |
| 16-sample averaging | Discards first read, averages 16 reads with 200µs gaps |
| EMA filter α=0.03 | Slow exponential moving average smooths remaining jitter |
| Deadband (±12 counts / 50ms) | Suppresses micro-fluctuations from triggering WebSocket sends |

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
| ESP32 reads zero or nothing | Check shared ground; confirm divider junction is under 3.3V; use GPIO32–39 only |
| Readings still fluctuate wildly | Add 0.1µF cap on GPIO34 to GND; confirm `analogReadResolution` / `analogSetAttenuation` are in `setup()` only |

## Dependencies

| Dependency | Where |
|-----------|-------|
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | Arduino IDE Library Manager |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | Arduino IDE Library Manager |
| [NativeWebSocket](https://github.com/endel/NativeWebSocket) | Unity Package Manager (UPM) |
| [WSM Train Controller (Railroad System) v3.4](https://assetstore.unity.com/publishers/16163) | Unity Asset Store |
