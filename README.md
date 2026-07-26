# train-arduino

A physical-to-digital model railway installation. A potentiometer on a real model train throttle is read by an ESP32 and streamed over WebSocket to Unity, where it drives a virtual locomotive via the **WSM Train Controller (Railroad System) v3.4** asset. Unity views are then distributed to salvaged screens around the installation space via a self-hosted [VDO.Ninja](https://github.com/jonathaniscarroll/vdo-ninja-embed) streaming layer. A second moving node — a XIAO ESP32S3 — rides on the train itself carrying a camera and scrolling LED matrix display.

---

## System Architecture

```
Physical throttle pot (~19V controller, 10kΩ pot)
        │
        ▼
  Voltage divider (R1=47kΩ, R2=10kΩ) → scales ~19V down to ~3.3V
        │
        ▼
  Fixed ESP32 — ADC GPIO34 (ADC1 — safe with Wi-Fi active)
  16-sample averaging + EMA filter (α=0.03)
        │  WebSocket  ws://<esp32-ip>/ws  (broadcasts "raw,filtered")
        ├──────────────────────────────────────────────┐
        ▼                                              ▼
  Unity NativeWebSocket client              XIAO ESP32S3 (on moving train)
  TrainSpeedReceiver.cs                     - WebSocket client, reads speed
  maps filtered ADC → KPH                  - MAX7219 matrix: scrolling text
  ILocomotive API (WSM v3.4)                  speed-synced via Parola
        │                                   - Camera → web stream
        ▼
  Unity locomotive (physics, SFX, signals all intact)
        │
        ▼  Unity Render Streaming (com.unity.renderstreaming)
  Multiple camera RenderTextures → separate WebRTC streams
        │  wss://<lan-ip>:8444 (local VDO.Ninja signaling)
        ▼
  Self-hosted VDO.Ninja  →  https://github.com/jonathaniscarroll/vdo-ninja-embed
  Each stream has a stable push ID (e.g. traincam-driver, traincam-bird)
        │
        ▼
  Salvaged screens on LAN — each opens:
  https://<lan-ip>/?view=<stream-id>&autoplay=1&cleanoutput=1
```

---

## Repository Structure

```
train-arduino/
├── esp32/
│   ├── WIRING.md
│   ├── train_pot_ws/
│   │   └── train_pot_ws.ino        # Fixed ESP32: pot → averaging → WebSocket server
│   └── camera_web_server/
│       └── CameraWebServer/        # ESP32-CAM web server sketch
├── unity/
│   ├── TrainSpeedReceiver.cs       # WebSocket client → ILocomotive API (primary)
│   ├── TrainPotReceiver.cs         # Raw pot receiver variant / debug
│   └── gage-train-unity/           # Unity project files
├── Train Controller (Railroad System) User Manual v3.4.pdf
└── README.md
```

> The VDO.Ninja self-hosted streaming server lives in a separate repo:
> **[jonathaniscarroll/vdo-ninja-embed](https://github.com/jonathaniscarroll/vdo-ninja-embed)**

---

## Wiring

> ⚠️ The controller runs at **~19V** — the pot wiper **must not** connect directly to the ESP32. A voltage divider is required.

### Pot Identification

| Measurement | Value |
|---|---|
| Outer pin to outer pin | ~10kΩ (reads `010` on multimeter at 2000k range) |
| Controller reference voltage | ~19V |
| Pot type | Linear, unmarked |

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

### Hardware Notes

- **Shared ground is mandatory** — ESP32 GND and controller GND must be the same node
- **Add a 0.1µF ceramic cap** from GPIO34 to GND (close to the ESP32 pin) to reduce noise
- **Use ADC1 pins only** (GPIO32–39) while Wi-Fi is active — ADC2 is disabled by Wi-Fi
- **Measure before connecting** — confirm divider junction stays under 3.3V at max throttle

### Debug Checklist

- [ ] ESP32 GND tied to controller GND?
- [ ] Divider junction (between R1 and R2) connected to GPIO34 — not the raw wiper?
- [ ] Capacitor on GPIO34 to GND?
- [ ] Using GPIO34–39 (ADC1), not GPIO0/2/4/12–15/25–27 (ADC2)?
- [ ] `analogReadResolution` and `analogSetAttenuation` called in `setup()` only?
- [ ] Divider output measured with meter before connecting ESP32 — confirmed under 3.3V?

---

## ESP32 Setup (Fixed Controller Node)

1. Open `esp32/train_pot_ws/train_pot_ws.ino` in Arduino IDE
2. Set your WiFi SSID and password at the top of the sketch
3. Install required Arduino libraries:
   - [`ESPAsyncWebServer`](https://github.com/ESP32Async/ESPAsyncWebServer)
   - `AsyncTCP`
4. Wire up the voltage divider as shown above
5. Upload to the ESP32
6. Open Serial Monitor at **115200 baud** — the local IP will be printed on connect
7. Visit that IP in a browser to confirm live pot readings

### ADC Configuration

These must be set **once in `setup()`** — do not call them inside the read loop:

```cpp
analogReadResolution(12);       // 0–4095 range
analogSetAttenuation(ADC_11db); // 0–3.3V input range
```

### WebSocket Message Format

The ESP32 broadcasts whenever the value changes meaningfully:

```
raw,filtered
```

Example: `2051,2048`

- **raw** — 16-sample averaged 12-bit ADC reading (0–4095)
- **filtered** — EMA-smoothed value (α=0.03); used by `TrainSpeedReceiver.cs` and the XIAO traincam node

### Noise Reduction

| Layer | What it does |
|---|---|
| 0.1µF cap on GPIO34 | Filters high-frequency hardware noise |
| 16-sample averaging | Discards first read, averages 16 reads with 200µs gaps |
| EMA filter α=0.03 | Slow exponential moving average smooths remaining jitter |
| Deadband (±12 counts / 50ms) | Suppresses micro-fluctuations from triggering WebSocket sends |

---

## XIAO ESP32S3 Traincam Node (Moving Train)

A **XIAO ESP32S3 Sense** rides on the train itself, acting as a second Wi-Fi node. It:
- Serves a camera stream via a simple HTTP web interface
- Drives MAX7219 LED matrix modules (pins D10, D8, D9) with Parola scrolling text
- Connects as a **WebSocket client** to the fixed ESP32's `/ws` endpoint and uses the `filtered` speed value to scale the Parola scroll speed — so the text motion feels synchronised with train movement

### Power

Power the XIAO from its **5V/VUSB** side with a solid ground reference. Add a diode only if USB and external power will be connected simultaneously. The camera and LED matrix together are sensitive to weak 5V supply — use a stable dedicated supply, not a marginal regulator.

### Known Risks

| Risk | Mitigation |
|---|---|
| `traincam.local` mDNS inconsistent | Connect by IP first; mDNS is for convenience only |
| Web server loop stall | Ensure the HTTP loop is serviced every cycle; avoid blocking delays in sketch |
| Power brownout | Measure 5V under full camera + matrix load before deployment |

---

## Unity Setup

### Requirements

- **WSM Train Controller (Railroad System) v3.4** — Unity Asset Store
- **NativeWebSocket** — install via UPM:
  ```
  https://github.com/endel/NativeWebSocket.git#upm
  ```
- **Unity Render Streaming** (`com.unity.renderstreaming`) — for multi-screen streaming; install via Package Manager

### Throttle Bridge

1. Import NativeWebSocket into your project via the Package Manager
2. Copy `unity/TrainSpeedReceiver.cs` into your Unity Assets folder
3. Attach `TrainSpeedReceiver` to any GameObject in your scene
4. In the Inspector:

| Field | Description |
|-------|-------------|
| **Esp32 Ip** | IP address printed by ESP32 on Serial Monitor |
| **Locomotive Object** | Your locomotive's GameObject (`TrainController_v3` or `SplineBasedLocomotive`) |
| **Max Speed Kph** | Unity train's top speed at full pot deflection. Keep ≤ 105 for physics-based. |
| **Invert Speed** | Tick if turning the pot up slows the Unity train instead of speeding it up |
| **Speed Smoothing** | `0.001–1` — lower = snappier, higher = gradual locomotive-style acceleration |
| **Deadband** | Normalised ADC threshold below which the train is treated as fully stopped |

### Multi-Camera Streaming to Installation Screens

Unity streams different views to different screens using **Unity Render Streaming** + the self-hosted VDO.Ninja layer.

#### Unity side

1. Create one `Camera` per content source — e.g. `CamDriver` (cab view), `CamBird` (overhead), `CamSignal` (signals/HUD), `CamMap` (top-down layout)
2. Assign each camera a `RenderTexture` sized for its target screen (e.g. 1280×720)
3. Add a `VideoStreamSender` component to each camera
4. Implement a `MultiCameraSignalingHandler` so each WebRTC connection ID maps to a distinct camera (see [detach8/multi-camera-unity-render-streaming](https://github.com/detach8/multi-camera-unity-render-streaming) as a reference)
5. Point Unity Render Streaming's signaling at the local VDO.Ninja WSS endpoint:
   ```
   wss://<lan-ip>:8444
   ```

#### VDO.Ninja side

Each Unity camera pushes to the local VDO.Ninja server with a stable stream ID:

| Camera | Push URL |
|---|---|
| CamDriver | `https://<lan-ip>/?push=traincam-driver` |
| CamBird | `https://<lan-ip>/?push=traincam-bird` |
| CamSignal | `https://<lan-ip>/?push=traincam-signal` |
| CamMap | `https://<lan-ip>/?push=traincam-map` |

Each salvaged screen opens a browser with its assigned view:

```
https://<lan-ip>/?view=traincam-driver&autoplay=1&cleanoutput=1
https://<lan-ip>/?view=traincam-bird&autoplay=1&cleanoutput=1
```

`&cleanoutput=1` strips the VDO.Ninja UI so only the raw video fills the screen — suitable for unattended installation displays.

> **VDO.Ninja server repo:** [jonathaniscarroll/vdo-ninja-embed](https://github.com/jonathaniscarroll/vdo-ninja-embed)
> See that repo's README for how to run the local HTTPS web server and WSS signaling server together with `lan-run.js`.

---

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
| Screen shows wrong stream | Check `?view=` ID matches the `?push=` ID used by Unity for that camera |
| Screens can't reach VDO.Ninja | Confirm LAN IP hasn't changed; check `lan-run.js` is running in vdo-ninja-embed repo |

---

## Dependencies

| Dependency | Where |
|-----------|-------|
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | Arduino IDE Library Manager |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | Arduino IDE Library Manager |
| [NativeWebSocket](https://github.com/endel/NativeWebSocket) | Unity Package Manager (UPM) |
| [WSM Train Controller (Railroad System) v3.4](https://assetstore.unity.com/publishers/16163) | Unity Asset Store |
| [Unity Render Streaming](https://docs.unity3d.com/Packages/com.unity.renderstreaming@3.1/manual/index.html) | Unity Package Manager |
| [vdo-ninja-embed](https://github.com/jonathaniscarroll/vdo-ninja-embed) | Separate repo — local VDO.Ninja server |
