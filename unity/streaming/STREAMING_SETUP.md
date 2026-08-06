# Unity Multi-Camera Render Streaming — Setup Guide

This folder contains the Unity scripts for streaming different views to different
installation screens via the local VDO.Ninja server
([jonathaniscarroll/vdo-ninja-embed](https://github.com/jonathaniscarroll/vdo-ninja-embed)).

---

## Package Requirements

Install both packages via **Window → Package Manager → Add by name**:

| Package | Name |
|---|---|
| Unity Render Streaming | `com.unity.renderstreaming` |
| Unity WebRTC | `com.unity.webrtc` |

Minimum Unity version: **2021.3 LTS**  
GPU encoding requires: **NVIDIA GTX 1050+ or AMD RX 500+** on the host machine.

---

## Scene Setup

### 1. RenderStreaming component

Create an empty GameObject called `StreamingManager` and add:
- `RenderStreaming` component
- `MultiCameraStreamingHandler` component (this folder)
- `StreamHealthMonitor` component (optional, for diagnostics)

In the `RenderStreaming` component Inspector:
- **Signaling Type** → `WebSocket`
- **Signaling URL** → `wss://192.168.1.100:8444`
  *(replace with your machine's LAN IP — run `lan-run.js` in vdo-ninja-embed first)*
- **Run On Awake** → ✓ enabled

### 2. Camera assignment

In the `MultiCameraStreamingHandler` Inspector, add one entry per screen:

| Stream ID | Camera | Suggested Resolution |
|---|---|---|
| `traincam-driver` | CamDriver | 1280 × 720 |
| `traincam-bird` | CamBird | 1280 × 720 |
| `traincam-signal` | CamSignal | 1280 × 720 |
| `traincam-map` | CamMap (orthographic) | 1920 × 1080 |

If you leave the RenderTexture field blank the script auto-creates a 1280×720
one at runtime. Pre-creating RenderTextures as Unity assets is recommended for
stable memory and Inspector preview.

### 3. Camera rig

Add `TrainCameraRig` to the locomotive's root GameObject.  
Drag the four cameras into the matching fields.  
CamMap should **not** be parented to the locomotive — assign it as a world-fixed
Camera in the scene, then drag it into the CamMap field.

---

## VDO.Ninja Side

With `lan-run.js` running in `vdo-ninja-embed`, Unity pushes each stream to the
local server using its stream ID. On each salvaged screen, open a browser and
navigate to:

```
https://<lan-ip>/?view=traincam-driver&autoplay=1&cleanoutput=1
https://<lan-ip>/?view=traincam-bird&autoplay=1&cleanoutput=1
https://<lan-ip>/?view=traincam-signal&autoplay=1&cleanoutput=1
https://<lan-ip>/?view=traincam-map&autoplay=1&cleanoutput=1
```

`&cleanoutput=1` removes all VDO.Ninja UI so only the video fills the screen —
ideal for unattended installation displays.

**To bookmark a screen permanently:** use the IP, not `localhost` or `.local`,
so the link survives network changes on the display device.

---

## Commissioning Checklist

- [ ] `lan-run.js` running in `vdo-ninja-embed` — confirm HTTPS and WSS ports are up
- [ ] Unity Render Streaming package installed (`com.unity.renderstreaming`)
- [ ] `StreamingManager` GameObject in scene with `RenderStreaming` component
- [ ] Signaling URL set to `wss://<your-lan-ip>:8444` in Inspector
- [ ] All four cameras assigned in `MultiCameraStreamingHandler`
- [ ] `TrainCameraRig` attached to locomotive root, CamMap world-fixed
- [ ] Press Play in Unity — check console for `[MultiCameraStreamingHandler] Registered stream ...`
- [ ] Open each `?view=` URL on its target screen and confirm video appears
- [ ] Adjust throttle pot — confirm Unity speed mirrors it and all streams update

---

## Troubleshooting

| Problem | Fix |
|---|---|
| No video on screen | Check signaling URL is reachable from Unity host; confirm `?push=` ID matches `?view=` ID |
| `VideoStreamSender` not found | Ensure `com.unity.renderstreaming` is installed and the package version matches Unity version |
| GPU encoding error | Verify NVIDIA/AMD GPU is present; software fallback can be forced via `VideoStreamSender` settings |
| Screen shows black | RenderTexture may not be created — check auto-create log message in Unity console |
| mDNS / `.local` fails on screen | Use IP address directly in the `?view=` URL |
| All screens show same view | Stream IDs must be unique per camera entry in `MultiCameraStreamingHandler` |
