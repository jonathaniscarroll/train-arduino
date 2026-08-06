using UnityEngine;

/// <summary>
/// Attaches to the locomotive GameObject and manages the four installation cameras.
/// Parent this script's GameObject to the locomotive so the cameras follow the train.
///
/// Camera layout:
///   CamDriver  — forward-facing cab view, offset to driver position
///   CamBird    — overhead follow cam, fixed height above loco
///   CamSignal  — angled down-track view for showing signals
///   CamMap     — orthographic top-down, fixed world position (not parented)
/// </summary>
public class TrainCameraRig : MonoBehaviour
{
    [Header("Driver / Cab View")]
    public Camera camDriver;
    public Vector3 driverOffset = new Vector3(0f, 1.2f, 0.8f);

    [Header("Bird's-Eye Follow")]
    public Camera camBird;
    public float birdHeight = 8f;
    public float birdLookDownAngle = 70f;

    [Header("Signal / Down-Track View")]
    public Camera camSignal;
    public Vector3 signalOffset = new Vector3(0f, 2.5f, -1.5f);
    public float signalLookForward = 25f; // degrees tilt down

    [Header("Top-Down Map (world-fixed)")]
    public Camera camMap;
    [Tooltip("World-space centre of the layout")]
    public Vector3 mapCenter = Vector3.zero;
    public float mapHeight = 20f;

    void LateUpdate()
    {
        if (camDriver != null)
        {
            camDriver.transform.localPosition = driverOffset;
            camDriver.transform.localRotation = Quaternion.identity;
        }

        if (camBird != null)
        {
            camBird.transform.position = transform.position + Vector3.up * birdHeight;
            camBird.transform.rotation = Quaternion.Euler(birdLookDownAngle, transform.eulerAngles.y, 0f);
        }

        if (camSignal != null)
        {
            camSignal.transform.localPosition = signalOffset;
            camSignal.transform.localRotation = Quaternion.Euler(signalLookForward, 0f, 0f);
        }

        if (camMap != null)
        {
            // Map cam stays fixed — just keep it pointing straight down
            camMap.transform.position = mapCenter + Vector3.up * mapHeight;
            camMap.transform.rotation = Quaternion.Euler(90f, 0f, 0f);
            if (!camMap.orthographic)
            {
                camMap.orthographic = true;
                camMap.orthographicSize = 12f;
            }
        }
    }
}
