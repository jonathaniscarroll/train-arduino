using System.Globalization;
using NativeWebSocket;
using UnityEngine;
using WSMGameStudio.RailroadSystem;

// Requires: WSM Game Studio - Train Controller (Railroad System) v3.4
// Both TrainController_v3 (physics) and SplineBasedLocomotive (spline) expose ILocomotive.

/// <summary>
/// Receives the physical model train's potentiometer speed from the ESP32 over WebSocket
/// and applies it to a WSM Train Controller (Railroad System) v3.4 locomotive via ILocomotive.
///
/// The ESP32 sends "raw,filtered" comma-separated ADC values (0-4095).
/// The filtered value is mapped to a 0-maxSpeedKph range and written to locomotive.MaxSpeed.
/// Acceleration is held at 1 (forward) so the locomotive always drives in one direction.
/// Set Acceleration = -1 in the Inspector override to reverse, or tick Invert.
///
/// Setup:
///   1. Attach this script to any GameObject in the scene.
///   2. Assign the Locomotive field to your train's locomotive GameObject
///      (must have TrainController_v3 or SplineBasedLocomotive component).
///   3. Set Esp32 Ip to the IP printed by the ESP32 on Serial Monitor.
///   4. Press Play - turn the pot and the Unity train mirrors the physical train's speed.
/// </summary>
public class TrainSpeedReceiver : MonoBehaviour
{
    // ── Connection ────────────────────────────────────────────────────────────
    [Header("Connection")]
    [Tooltip("IP address shown in Arduino Serial Monitor after ESP32 connects to WiFi")]
    public string esp32Ip = "192.168.1.50";

    // ── Locomotive ────────────────────────────────────────────────────────────
    [Header("Locomotive")]
    [Tooltip("The locomotive GameObject. Must have TrainController_v3 or SplineBasedLocomotive.")]
    public GameObject locomotiveObject;

    [Tooltip("Maximum speed in KPH at full pot deflection (ADC 4095). " +
             "Physics-based trains: keep at or below 105 kph. Spline-based: no limit.")]
    public float maxSpeedKph = 60f;

    [Tooltip("Flip pot direction if turning the physical pot up makes the Unity train slow down.")]
    public bool invertSpeed = false;

    // ── Feel ──────────────────────────────────────────────────────────────────
    [Header("Feel")]
    [Tooltip("How quickly the Unity train accelerates / decelerates toward the target speed. " +
             "Lower = snappier, higher = more gradual (like locomotive inertia).")]
    [Range(0.001f, 1f)]
    public float speedSmoothing = 0.08f;

    [Tooltip("Normalised pot value below which the train is treated as stopped. " +
             "Prevents ADC jitter from keeping the engine creeping.")]
    [Range(0f, 0.05f)]
    public float deadband = 0.01f;

    // ── Runtime ───────────────────────────────────────────────────────────────
    WebSocket _ws;
    ILocomotive _loco;
    float _targetNorm  = 0f;   // normalised 0-1 speed from latest WebSocket message
    float _currentNorm = 0f;   // smoothed normalised speed driving MaxSpeed

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    async void Start()
    {
        // Resolve ILocomotive from the assigned GameObject
        if (locomotiveObject != null)
            _loco = locomotiveObject.GetComponent<ILocomotive>();

        if (_loco == null)
        {
            Debug.LogError("[TrainSpeed] No ILocomotive component found on locomotiveObject. " +
                           "Assign a GameObject with TrainController_v3 or SplineBasedLocomotive.");
            return;
        }

        // Start the engine and set it driving forward
        _loco.EnginesOn    = true;
        _loco.Acceleration = 1f;     // 1 = forward, 0 = stop, -1 = reverse (per WSM manual §3.1)
        _loco.AutomaticBrakes = true; // let the asset handle braking when speed drops to 0
        _loco.MaxSpeed = 0f;          // start stationary; pot will ramp up

        // Connect WebSocket
        _ws = new WebSocket($"ws://{esp32Ip}/ws");
        _ws.OnOpen    += ()  => Debug.Log("[TrainSpeed] WebSocket connected");
        _ws.OnError   += (e) => Debug.LogError($"[TrainSpeed] WebSocket error: {e}");
        _ws.OnClose   += (e) => Debug.Log($"[TrainSpeed] WebSocket closed: {e}");
        _ws.OnClose += (e) => restart();
        _ws.OnMessage += OnMessage;

        await _ws.Connect();
    }

    async void restart()
    {
        if (_ws != null) await _ws.Close();
        _ws = new WebSocket($"ws://{esp32Ip}/ws");
        _ws.OnOpen += () => Debug.Log("[TrainSpeed] WebSocket connected");
        _ws.OnError += (e) => Debug.LogError($"[TrainSpeed] WebSocket error: {e}");
        _ws.OnClose += (e) => Debug.Log($"[TrainSpeed] WebSocket closed: {e}");
        _ws.OnClose += (e) => restart();
        _ws.OnMessage += OnMessage;

        await _ws.Connect();
    }

    /// <summary>
    /// Parses "raw,filtered" messages broadcast by the ESP32.
    /// Uses the FILTERED value (parts[1]) — the EMA-smoothed ADC reading.
    /// </summary>
    void OnMessage(byte[] bytes)
    {
        var msg   = System.Text.Encoding.UTF8.GetString(bytes).Trim();
        var parts = msg.Split(',');
        if (parts.Length < 2) return;

        if (!int.TryParse(parts[1].Trim(),
                          NumberStyles.Integer,
                          CultureInfo.InvariantCulture,
                          out int filteredRaw))
            return;

        float norm = Mathf.Clamp01(filteredRaw / 4095f);
        _targetNorm = invertSpeed ? 1f - norm : norm;
    }

    void Update()
    {
        // Dispatch queued messages on non-WebGL builds
#if !UNITY_WEBGL || UNITY_EDITOR
        _ws?.DispatchMessageQueue();
#endif
        if (_loco == null) return;

        // Smooth toward target
        _currentNorm = Mathf.Lerp(_currentNorm, _targetNorm, speedSmoothing);

        // Deadband: below threshold = fully stopped
        float effective = (_currentNorm < deadband) ? 0f : _currentNorm;

        // Write MaxSpeed to the locomotive (WSM ILocomotive API, §11.1)
        // MaxSpeed is in the unit selected on the component (default Kph)
        _loco.MaxSpeed = effective * maxSpeedKph;

        // Keep engine running and accelerating; stop/reverse is done by MaxSpeed = 0
        // rather than by touching Acceleration so braking physics still apply.
        if (effective <= 0f)
        {
            // When pot is at zero, apply brakes to halt the train cleanly
            _loco.Brake = 1f;
        }
        else
        {
            _loco.Brake        = 0f;
            _loco.Acceleration = 1f;
        }
    }

    async void OnApplicationQuit()
    {
        if (_ws != null) await _ws.Close();
    }
}
