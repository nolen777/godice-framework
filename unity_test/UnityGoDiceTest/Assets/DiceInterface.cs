#if UNITY_IOS || UNITY_MACOSX || UNITY_EDITOR_OSX
#define USE_SWIFT_INTERFACE
#else
#undef USE_SWIFT_INTERFACE
#endif

using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using AOT;
using UnityEditor.VersionControl;
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
  
    [DllImport (dllName: "GodiceBundle", EntryPoint = "godice_set_roll_callback")]
    private static extern void GodiceSetRollCallback(DelegateMessage delegateMessage);
    
    private delegate void DelegateMessage(string name, byte x, byte y, byte z);
 
    [MonoPInvokeCallback(typeof(DelegateMessage))] 
    private static void DelegateMessageReceived(string name, byte x, byte y, byte z) {
        if (_singleton != null && _singleton.callback != null) {
            _singleton.callback(name, x, y, z);
        }
    }
#endif

    private static DiceInterface _singleton = null;
    
    public delegate void RollCallback(string name, byte x, byte y, byte z);
    public RollCallback callback = null;
    
    public void StartListening() {
#if USE_SWIFT_INTERFACE
        GodiceStartListening();
        GodiceSetRollCallback(DelegateMessageReceived);
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
