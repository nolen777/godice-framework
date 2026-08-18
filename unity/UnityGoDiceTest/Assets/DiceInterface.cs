#if UNITY_IOS || UNITY_STANDALONE_OSX || UNITY_EDITOR_OSX
#define USE_SWIFT_INTERFACE
#elif UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN || ENABLE_WINMD_SUPPORT
#define USE_WINDOWS_INTERFACE
#else
#endif

using System.Collections.Generic;
using UnityEngine;
using UnityGoDiceInterface;

public class DiceInterface : MonoBehaviour
{
    private readonly IDiceInterfaceImports diceInterfaceImports = new NativeDiceInterfaceImports();
    private readonly object pendingMessagesLock = new object();
    private List<PendingMessage> pendingMessages = new List<PendingMessage>();

    private struct PendingMessage {
        public string name;
        public List<byte> bytes;
    }

    private static byte[] rollVector(List<byte> rawData) {
        if (rawData.Count < 1) {
            Debug.Log("rollVector: no data");
            return null;
        }
        byte firstByte = rawData[0];
        if (firstByte != 83) {
            Debug.Log("rollVector: first byte is not 83");
            return null;
        }
        if (rawData.Count != 4) {
            Debug.Log("rollVector: data length is not 4");
            return null;
        }
        return new byte[] { rawData[1], rawData[2], rawData[3] };
    }
    
    private static void DelegateMessageReceived(string name, List<byte> byteList) {
        var instance = _singleton;
        if (instance == null) {
            return;
        }

        lock (instance.pendingMessagesLock) {
            instance.pendingMessages.Add(new PendingMessage { name = name, bytes = byteList });
        }
    }

    private void ProcessMessage(string name, List<byte> byteList) {
        if (byteList.Count == 0) {
            connectionCallback?.Invoke(name);
            return;
        }

        byte firstByte = byteList[0];

        switch (firstByte) {
            case 82:
                // Roll started
                break;
            case 66:
                // Battery level
                break;
            case 67:
                // Color (fetched)
                break;
            case 83: {
                var roll = rollVector(byteList);
                if (roll != null) {
                    rollCallback?.Invoke(name, roll[0], roll[1], roll[2]);
                }
                break;
            }
            case 70:
            case 84:
            case 77: {
                var stableMessage = byteList.GetRange(1, byteList.Count - 1);
                var roll = rollVector(stableMessage);
                if (roll != null) {
                    rollCallback?.Invoke(name, roll[0], roll[1], roll[2]);
                }
                break;
            }
            default:
                Debug.Log("Not yet handled");
                break;
        }
    }

    private static DiceInterface _singleton = null;

    public delegate void ConnectionCallback(string name);
    public delegate void RollCallback(string name, byte x, byte y, byte z);
    
    public ConnectionCallback connectionCallback = null;
    public RollCallback rollCallback = null;
    
    public void StartListening() {
        diceInterfaceImports.SetCallback(DelegateMessageReceived);
        diceInterfaceImports.StartListening();
    }
    
    public void StopListening() {
        diceInterfaceImports.StopListening();
    }
    
    void Awake() {
        _singleton = this;
    }

    void OnDestroy() {
        if (_singleton == this) {
            _singleton = null;
        }
    }

    void Update() {
        List<PendingMessage> toProcess;
        lock (pendingMessagesLock) {
            toProcess = pendingMessages;
            pendingMessages = new List<PendingMessage>();
        }

        foreach (var message in toProcess) {
            ProcessMessage(message.name, message.bytes);
        }
    }
}
