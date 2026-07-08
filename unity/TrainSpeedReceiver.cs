using System;
using System.Globalization;
using NativeWebSocket;
using UnityEngine;

/// <summary>
/// Connects to the ESP32 WebSocket server and drives a train's SPEED
/// based on the potentiometer filtered ADC value (0–4095).
///
/// The ESP32 sends comma-separated messages: "raw,filtered"
/// This script uses the filtered value to derive a normalised speed (0–1),
/// then moves the train along the track at that speed each frame.
///
/// Attach to your train GameObject.
/// Set esp32Ip to the IP printed by the ESP32 in Serial Monitor.
/// Assign trackStart and trackEnd to define the track endpoints in world space.
/// The train will loop: when it reaches trackEnd it teleports back to trackStart.
/// </summary>
public class TrainSpeedReceiver : MonoBehaviour
{
    // ── Connection ────────────────────────────────────────────────────────────
    [Header("Connection")]
    [Tooltip("IP address printed by the ESP32 in Serial Monitor")]
    public string esp32Ip = "192.168.1.50";

    // ── Track ─────────────────────────────────────────────────────────────────
    [Header("Track")]
    [Tooltip("World-space position where the track begins")]
    public Transform trackStart;

    [Tooltip("World-space position where the track ends")]
    public Transform trackEnd;

    [Tooltip("Flip the speed direction if your physical pot is wired in reverse")]
    public bool invertSpeed = false;

    // ── Speed ─────────────────────────────────────────────────────────────────
    [Header("Speed")]
    [Tooltip("Maximum world-units per second at full pot (ADC 4095)")]
    public float maxSpeed = 5f;

    [Tooltip("Speed smoothing: how quickly the train accelerates/decelerates (0=instant, 1=very slow)")]
    [Range(0f, 1f)]
    public float speedSmoothing = 0.12f;

    [Tooltip("Normalised speed threshold below which the train is considered stopped")]
    [Range(0f, 0.05f)]
    public float deadband = 0.01f;

    // ── Runtime state ─────────────────────────────────────────────────────────
    WebSocket _ws;
    float _targetSpeed = 0f;   // normalised 0–1 from WebSocket
    float _currentSpeed = 0f;  // smoothed normalised speed
    float _trackProgress = 0f; // 0 = trackStart, 1 = trackEnd

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    async void Start()
    {
        _ws = new WebSocket($"ws://{esp32Ip}/ws");

        _ws.OnOpen    += ()  => Debug.Log("[TrainSpeed] WebSocket connected");
        _ws.OnError   += (e) => Debug.LogError($"[TrainSpeed] Error: {e}");
        _ws.OnClose   += (e) => Debug.Log($"[TrainSpeed] Closed: {e}");
        _ws.OnMessage += OnMessage;

        await _ws.Connect();
    }

    /// <summary>
    /// Parses "raw,filtered" messages from the ESP32.
    /// Uses the FILTERED value to derive normalised speed.
    /// </summary>
    void OnMessage(byte[] bytes)
    {
        var msg = System.Text.Encoding.UTF8.GetString(bytes).Trim();

        // Expected format: "raw,filtered"  e.g. "2048,2052"
        var parts = msg.Split(',');
        if (parts.Length < 2) return;

        if (!int.TryParse(parts[1].Trim(),
                          NumberStyles.Integer,
                          CultureInfo.InvariantCulture,
                          out int filteredRaw))
            return;

        float normalised = Mathf.Clamp01(filteredRaw / 4095f);
        _targetSpeed = invertSpeed ? 1f - normalised : normalised;
    }

    void Update()
    {
        // Required for non-WebGL builds to dispatch queued messages
#if !UNITY_WEBGL || UNITY_EDITOR
        _ws?.DispatchMessageQueue();
#endif

        if (trackStart == null || trackEnd == null) return;

        // Smooth toward target speed
        _currentSpeed = Mathf.Lerp(_currentSpeed, _targetSpeed, speedSmoothing);

        // Apply deadband — treat very low values as stopped
        float effectiveSpeed = (_currentSpeed < deadband) ? 0f : _currentSpeed;

        // Advance track progress proportionally to speed and delta time
        float trackLength = Vector3.Distance(trackStart.position, trackEnd.position);
        if (trackLength < 0.001f) return;

        float worldUnitsThisFrame = effectiveSpeed * maxSpeed * Time.deltaTime;
        _trackProgress += worldUnitsThisFrame / trackLength;

        // Loop: wrap back to start when the train reaches the end
        if (_trackProgress >= 1f)
            _trackProgress -= 1f;

        // Position train along track
        transform.position = Vector3.Lerp(
            trackStart.position,
            trackEnd.position,
            _trackProgress
        );

        // Orient train to face direction of travel
        Vector3 dir = (trackEnd.position - trackStart.position).normalized;
        if (dir != Vector3.zero)
            transform.rotation = Quaternion.LookRotation(dir);
    }

    async void OnApplicationQuit()
    {
        if (_ws != null) await _ws.Close();
    }

    // ── Editor helpers ────────────────────────────────────────────────────────
    void OnDrawGizmosSelected()
    {
        if (trackStart == null || trackEnd == null) return;
        Gizmos.color = Color.cyan;
        Gizmos.DrawLine(trackStart.position, trackEnd.position);
        Gizmos.DrawWireSphere(trackStart.position, 0.15f);
        Gizmos.color = Color.green;
        Gizmos.DrawWireSphere(trackEnd.position, 0.15f);
    }
}
