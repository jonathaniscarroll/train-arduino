using System.Collections;
using UnityEngine;

/// <summary>
/// Logs stream health to the Unity console at a set interval.
/// Attach to the same GameObject as MultiCameraStreamingHandler.
/// Useful during installation setup to confirm all streams are live.
/// </summary>
[RequireComponent(typeof(MultiCameraStreamingHandler))]
public class StreamHealthMonitor : MonoBehaviour
{
    [Tooltip("How often to log stream status (seconds)")]
    public float checkIntervalSeconds = 10f;

    private MultiCameraStreamingHandler _handler;

    void Start()
    {
        _handler = GetComponent<MultiCameraStreamingHandler>();
        StartCoroutine(MonitorLoop());
    }

    IEnumerator MonitorLoop()
    {
        while (true)
        {
            yield return new WaitForSeconds(checkIntervalSeconds);
            foreach (var stream in _handler.cameraStreams)
            {
                var sender = _handler.GetSender(stream.streamId);
                string status = sender != null ? "OK" : "NOT FOUND";
                Debug.Log($"[StreamHealth] {stream.streamId} → {status}");
            }
        }
    }
}
