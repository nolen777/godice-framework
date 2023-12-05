#if UNITY_EDITOR_OSX || UNITY_STANDALONE_OSX || UNITY_IOS

using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using AOT;
using UnityEngine;


namespace UnityGoDiceInterface {
    public class AppleDiceInterfaceImports : IDiceInterfaceImports {
#if UNITY_IOS
        private const string DLLName = "__Internal";
#elif UNITY_STANDALONE_OSX || UNITY_EDITOR_OSX
        private const string DLLName = "GodiceBundle";
#endif
        
        private delegate void MonoDelegateMessage(string name, UInt32 byteCount, IntPtr bytePtr);
    
        [DllImport (dllName: DLLName, EntryPoint="godice_start_listening")]
        private static extern void _NativeBridgeStartListening();
  
        [DllImport (dllName: DLLName, EntryPoint = "godice_stop_listening")]
        private static extern void _NativeBridgeStopListening();
  
        [DllImport (dllName: DLLName, EntryPoint = "godice_set_callback")]
        private static extern void _NativeBridgeSetCallback(MonoDelegateMessage monoDelegateMessage);

        private static List<byte> BytesFromRawPointer(UInt32 byteCount, IntPtr bytes) {
            byte[] array = new byte[byteCount];
            Marshal.Copy(bytes, array, 0, (int)byteCount);
            return new List<byte>(array);
        }
    
        [MonoPInvokeCallback(typeof(MonoDelegateMessage))]
        private static void MonoDelegateMessageReceived(string name, UInt32 byteCount, IntPtr bytePtr) {
            if (IDiceInterfaceImports.Delegate != null) {
                IDiceInterfaceImports.Delegate(name, BytesFromRawPointer(byteCount, bytePtr));
            }
        }
        
        public void StartListening() {
            _NativeBridgeSetCallback(MonoDelegateMessageReceived);
            _NativeBridgeStartListening();
        }
        
        public void StopListening() {
            _NativeBridgeStopListening();
        }
    }
}
#endif
