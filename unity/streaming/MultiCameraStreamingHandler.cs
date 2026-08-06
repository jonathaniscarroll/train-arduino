using System.Collections.Generic;
using Unity.RenderStreaming;
using UnityEngine;

/// <summary>
/// Manages multiple Unity cameras as independent WebRTC video streams.
/// Each camera is assigned a stable stream ID that maps to a VDO.Ninja push URL.
///
/// Stream ID → Camera mapping (configure in Inspector):
///   traincam-driver  → Cab / driver's-eye view
///   traincam-bird    → Overhead / bird's-eye view
///   traincam-signal  → Signals and HUD overlay
///   traincam-map     → Top-down layout map
///
/// Each screen on the LAN opens:
///   https://<lan-ip>/?view=<stream-id>&autoplay=1&cleanoutput=1
/// </summary>
public class MultiCameraStreamingHandler : MonoBehaviour
{
    [System.Serializable]
    public class CameraStream
    {
        [Tooltip("Must match the VDO.Ninja push ID e.g. traincam-driver")]
        public string streamId;

        [Tooltip("The camera whose output is sent on this stream")]
        public Camera sourceCamera;

        [Tooltip("RenderTexture target — set resolution to match your screen")]
        public RenderTexture renderTexture;

        [HideInInspector] public VideoStreamSender sender;
    }

    [Header("Stream Definitions")]
    [Tooltip("One entry per installation screen. Stream IDs must be unique.")]
    public List<CameraStream> cameraStreams = new List<CameraStream>();

    [Header("Signaling")]
    [Tooltip("Local VDO.Ninja WSS endpoint — e.g. wss://192.168.1.100:8444")]
    public string signalingUrl = "wss://192.168.1.100:8444";

    private HttpSignaling _signaling;

    void Awake()
    {
        foreach (var stream in cameraStreams)
        {
            if (stream.sourceCamera == null)
            {
                Debug.LogWarning($"[MultiCameraStreamingHandler] Stream '{stream.streamId}' has no camera assigned — skipping.");
                continue;
            }

            if (stream.renderTexture == null)
            {
                // Auto-create a 1280x720 RenderTexture if none assigned
                stream.renderTexture = new RenderTexture(1280, 720, 24, RenderTextureFormat.ARGB32);
                stream.renderTexture.name = $"RT_{stream.streamId}";
                stream.renderTexture.Create();
                Debug.Log($"[MultiCameraStreamingHandler] Auto-created RenderTexture for '{stream.streamId}'");
            }

            stream.sourceCamera.targetTexture = stream.renderTexture;

            // Add VideoStreamSender to the camera's GameObject
            stream.sender = stream.sourceCamera.gameObject.AddComponent<VideoStreamSender>();
            stream.sender.SetTexture(stream.renderTexture);

            Debug.Log($"[MultiCameraStreamingHandler] Registered stream '{stream.streamId}' → {stream.sourceCamera.name}");
        }
    }

    void Start()
    {
        // Note: Full signaling wiring requires the RenderStreaming component on a
        // GameObject in the scene, pointed at signalingUrl. This handler registers
        // the per-camera senders. See RenderTextureSettings.md for scene setup.
        Debug.Log($"[MultiCameraStreamingHandler] {cameraStreams.Count} stream(s) initialised. Signaling: {signalingUrl}");
    }

    /// <summary>
    /// Returns the VideoStreamSender for a given stream ID — useful for
    /// runtime diagnostics or dynamic quality changes.
    /// </summary>
    public VideoStreamSender GetSender(string streamId)
    {
        var match = cameraStreams.Find(s => s.streamId == streamId);
        return match?.sender;
    }

    void OnDestroy()
    {
        foreach (var stream in cameraStreams)
        {
            if (stream.renderTexture != null && stream.renderTexture.IsCreated())
                stream.renderTexture.Release();
        }
    }
}
