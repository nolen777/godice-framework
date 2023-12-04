#if UNITY_IOS || UNITY_STANDALONE_OSX || UNITY_EDITOR_OSX
#define USE_SWIFT_INTERFACE
#elif UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
#define USE_WINDOWS_INTERFACE
#else
#undef USE_SWIFT_INTERFACE
#endif

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using AOT;
using UnityEngine;

public class DiceInterface : MonoBehaviour
{
#if UNITY_IOS
    const string dllName = "__Internal";
#elif UNITY_STANDALONE_OSX || UNITY_EDITOR_OSX
    const string dllName = "GodiceBundle";
#endif
    
    private delegate void DelegateMessage(string name, List<byte> bytes);
    
#if USE_SWIFT_INTERFACE
    private delegate void MonoDelegateMessage(string name, UInt32 byteCount, IntPtr bytePtr);
    
    [DllImport (dllName: dllName, EntryPoint="godice_start_listening")]
    private static extern void GodiceStartListening();
  
    [DllImport (dllName: dllName, EntryPoint = "godice_stop_listening")]
    private static extern void GodiceStopListening();
  
    [DllImport (dllName: dllName, EntryPoint = "godice_set_callback")]
    private static extern void GodiceSetCallback(MonoDelegateMessage monoDelegateMessage);

    private static List<byte> bytesFromRawPointer(UInt32 byteCount, IntPtr bytes) {
        byte[] array = new byte[byteCount];
        Marshal.Copy(bytes, array, 0, (int)byteCount);
        return new List<byte>(array);
    }
    
    [MonoPInvokeCallback(typeof(MonoDelegateMessage))]
    private static void MonoDelegateMessageReceived(string name, UInt32 byteCount, IntPtr bytePtr) {
        DelegateMessageReceived(name, bytesFromRawPointer(byteCount, bytePtr));
    }
    
#elif USE_WINDOWS_INTERFACE
    private static void GodiceStartListening() {
        Debug.Log("GodiceStartListening() called but unsupported");
    }
    
    private static void GodiceStopListening() {
        Debug.Log("GodiceStartListening() called but unsupported");
    }
    
    private static void GodiceSetCallback(DelegateMessage delegateMessage) {
        Debug.Log("GodiceSetCallback() called but unsupported");
    }
#else
#endif

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
        if (_singleton != null) {
            if (byteList.Count == 0) {
                if (_singleton.connectionCallback != null) {
                    _singleton.connectionCallback(name);
                    return;
                }
            } else {
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
                        if (roll != null && _singleton.rollCallback != null) {
                            _singleton.rollCallback(name, roll[0], roll[1], roll[2]);
                        }
                        break;
                    }
                    case 70:
                    case 84:
                    case 77: {
                        byteList.RemoveAt(0);
                        var roll = rollVector(byteList);
                        if (roll != null && _singleton.rollCallback != null) {
                            _singleton.rollCallback(name, roll[0], roll[1], roll[2]);
                        }
                        break;
                    }
                    default:
                        Debug.Log("Not yet handled");
                        break;
                }
            }
        }
    }

    private static DiceInterface _singleton = null;

    public delegate void ConnectionCallback(string name);
    public delegate void RollCallback(string name, byte x, byte y, byte z);
    
    public ConnectionCallback connectionCallback = null;
    public RollCallback rollCallback = null;
    
    public void StartListening() {
#if USE_SWIFT_INTERFACE
        GodiceStartListening();
        GodiceSetCallback(MonoDelegateMessageReceived);
#else
        Debug.Log("StartListening() called");
#endif
    }
    
    public void StopListening() {
#if USE_SWIFT_INTERFACE
        GodiceStopListening();
#else
        Debug.Log("StopListening() called");
#endif
    }
    
    // Start is called before the first frame update
    void Start() {
        _singleton = this;
    }

    // Update is called once per frame
    void Update()
    {
        
    }
}
