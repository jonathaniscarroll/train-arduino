# ESP32 Wiring — Train Controller Pot

## Overview

The real train controller uses an **unmarked 10kΩ pot** running on a **~19V reference**.
The ESP32 ADC input is limited to **0–3.3V**, so a resistor voltage divider is required
between the pot wiper and GPIO34.

---

## Pot Identification

| Measurement | Value |
|---|---|
| Outer pin to outer pin | ~10kΩ (reads `010` on multimeter at 2000k range) |
| Controller reference voltage | ~19V |
| Pot type | Linear, unmarked |

---

## Wiring Diagram

```
Controller ref (~19V) ──── Pot outer pin A
Controller GND        ──── Pot outer pin B

Pot wiper ──────────────── Controller speed input node
           │
           ├── R1 (47kΩ) ──┬── ESP32 GPIO34 (ADC1)
                           │
                         R2 (10kΩ)
                           │
Controller GND ────────────┴── ESP32 GND
```

### Divider math

With R1 = 47kΩ and R2 = 10kΩ:

```
V_gpio34 = 19V × (10k / (47k + 10k)) ≈ 3.33V at full throttle
```

This keeps GPIO34 at or just under 3.3V across the full pot sweep.

---

## Key Hardware Notes

- **Shared ground is mandatory.** ESP32 GND and controller GND must be the same node.
  If they are floating relative to each other, readings will be wrong or zero.
- **Add a 0.1µF ceramic capacitor** from GPIO34 to GND, physically close to the ESP32
  pin. This reduces high-frequency noise picked up from the 19V circuit and Wi-Fi.
- **Use ADC1 pins only** (GPIO32–39) while Wi-Fi is active. ADC2 is disabled by Wi-Fi.
- **Measure before connecting the ESP32.** Use a multimeter to confirm the divider
  junction voltage stays under 3.3V at max throttle before plugging into GPIO34.

---

## ADC Configuration (set once in `setup()`)

```cpp
analogReadResolution(12);       // 0–4095 range
analogSetAttenuation(ADC_11db); // 0–3.3V input range
```

Do **not** call these inside the read loop — calling them repeatedly adds noise.

---

## Noise Reduction Strategy

ESP32 ADC is noisy, especially when Wi-Fi is active. Three layers of mitigation are used:

1. **Hardware cap** — 0.1µF ceramic on GPIO34 to GND
2. **16-sample averaging** — discard first read, average 16 reads with 200µs gaps
3. **EMA filter** — exponential moving average with α = 0.03 (slow smoothing)
4. **Deadband** — only send WebSocket update if change > 12 ADC counts or > 50ms elapsed

---

## Quick Debug Checklist

- [ ] ESP32 GND tied to controller GND?
- [ ] Divider junction (between R1 and R2) connected to GPIO34 — not the raw wiper?
- [ ] Capacitor on GPIO34 to GND?
- [ ] Using GPIO34–39 (ADC1), not GPIO0/2/4/12–15/25–27 (ADC2)?
- [ ] `analogReadResolution` and `analogSetAttenuation` called in `setup()` only?
- [ ] Divider output measured with meter before connecting ESP32 — confirmed under 3.3V?
