#if UNITY_IOS || UNITY_MACOSX || UNITY_EDITOR_OSX
#define USE_SWIFT_INTERFACE
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
    [DllImport ("__Internal")]
  private static extern void godice_start_listening();
  
    [DllImport ("__Internal")]
  private static extern void godice_stop_listening ();
#else
    [DllImport (dllName: "GodiceBundle", EntryPoint="godice_start_listening")]
    private static extern void GodiceStartListening();
  
    [DllImport (dllName: "GodiceBundle", EntryPoint = "godice_stop_listening")]
    private static extern void GodiceStopListening();
  
    [DllImport (dllName: "GodiceBundle", EntryPoint = "godice_set_callback")]
    private static extern void GodiceSetCallback(DelegateMessage delegateMessage);
    
    private delegate void DelegateMessage(string name, UInt32 byteCount, IntPtr bytes);

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

    private static List<byte> bytesFromRawPointer(UInt32 byteCount, IntPtr bytes) {
        byte[] array = new byte[byteCount];
        Marshal.Copy(bytes, array, 0, (int)byteCount);
        return new List<byte>(array);
    }
    
    [MonoPInvokeCallback(typeof(DelegateMessage))] 
    private static void DelegateMessageReceived(string name, UInt32 byteCount, IntPtr bytePtr) {
        if (_singleton != null) {
            if (byteCount == 0) {
                if (_singleton.connectionCallback != null) {
                    _singleton.connectionCallback(name);
                    return;
                }
            } else {
                List<byte> byteList = bytesFromRawPointer(byteCount, bytePtr);

                byte firstByte = byteList[0];

                switch (firstByte) {
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
#endif

    private static DiceInterface _singleton = null;

    public delegate void ConnectionCallback(string name);
    public delegate void RollCallback(string name, byte x, byte y, byte z);
    
    public ConnectionCallback connectionCallback = null;
    public RollCallback rollCallback = null;
    
    public void StartListening() {
#if USE_SWIFT_INTERFACE
        GodiceStartListening();
        GodiceSetCallback(DelegateMessageReceived);
#else
        Debug.Log("StartListening() called");
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
