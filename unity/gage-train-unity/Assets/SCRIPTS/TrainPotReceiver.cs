using System.Globalization;
using NativeWebSocket;
using UnityEngine;

/// <summary>
/// Connects to the ESP32 WebSocket server and drives a train transform
/// based on the potentiometer value (ADC 0–4095).
///
/// Attach this script to your train GameObject.
/// Set esp32Ip to the IP printed by the ESP32 in Serial Monitor.
/// Assign startPos and endPos to define the track endpoints.
/// </summary>
public class TrainPotReceiver : MonoBehaviour
{
    [Header("Connection")]
    public string esp32Ip = "192.168.1.50";

    [Header("Train")]
    public Transform train;
    public Vector3 startPos;
    public Vector3 endPos;
    public bool invert = false;

    [Header("Motion")]
    [Tooltip("How quickly the train interpolates toward the target position")]
    public float smoothSpeed = 8f;

    WebSocket ws;
    float targetT = 0f;

    async void Start()
    {
        ws = new WebSocket($"ws://{esp32Ip}/ws");

        ws.OnOpen    += ()  => Debug.Log("[TrainPot] WebSocket connected");
        ws.OnError   += (e) => Debug.LogError($"[TrainPot] Error: {e}");
        ws.OnClose   += (e) => Debug.Log($"[TrainPot] Closed: {e}");

        ws.OnMessage += (bytes) =>
        {
            var msg = System.Text.Encoding.UTF8.GetString(bytes);
            if (int.TryParse(msg, NumberStyles.Integer, CultureInfo.InvariantCulture, out int raw))
            {
                float t = Mathf.Clamp01(raw / 4095f);
                if (invert) t = 1f - t;
                targetT = t;
            }
        };

        await ws.Connect();
    }

    void Update()
    {
        // Required for non-WebGL builds to process incoming messages
#if !UNITY_WEBGL || UNITY_EDITOR
        ws?.DispatchMessageQueue();
#endif
        if (train == null) return;

        Vector3 target = Vector3.Lerp(startPos, endPos, targetT);
        train.position = Vector3.Lerp(train.position, target, Time.deltaTime * smoothSpeed);
    }

    private async void OnApplicationQuit()
    {
        if (ws != null) await ws.Close();
    }
}
