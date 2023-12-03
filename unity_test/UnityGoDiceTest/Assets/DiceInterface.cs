#if UNITY_IOS || UNITY_MACOSX || UNITY_EDITOR_OSX
#define USE_SWIFT_INTERFACE
#else
#undef USE_SWIFT_INTERFACE
#endif

using System;
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
 
    [MonoPInvokeCallback(typeof(DelegateMessage))] 
    private static void DelegateMessageReceived(string name, UInt32 byteCount, IntPtr bytes) {
        if (_singleton != null && _singleton.callback != null) {
            byte[] array = new byte[byteCount];
            Marshal.Copy((IntPtr)bytes, array, 0, (int)byteCount);
            _singleton.callback(name, array);
        }
    }
#endif

    private static DiceInterface _singleton = null;
    
    public delegate void RollCallback(string name, Byte[] bytes);
    public RollCallback callback = null;
    
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
