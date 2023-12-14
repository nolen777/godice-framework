using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace UnityGoDiceInterface {
    public class NativeDiceInterfaceImports : IDiceInterfaceImports {
        private const string BundleName = "BleWinrtDll.dll";
        
        private delegate void MonoDelegateMessage(string name, UInt32 byteCount, IntPtr bytePtr);
    
        [DllImport (dllName: BundleName, EntryPoint = "godice_start_listening")]
        private static extern void _NativeBridgeStartListening();
  
        [DllImport (dllName: BundleName, EntryPoint = "godice_stop_listening")]
        private static extern void _NativeBridgeStopListening();
  
        [DllImport (dllName: BundleName, EntryPoint = "godice_set_callback")]
        private static extern void _NativeBridgeSetCallback(MonoDelegateMessage monoDelegateMessage);

        private static List<byte> BytesFromRawPointer(UInt32 byteCount, IntPtr bytes) {
            byte[] array = new byte[byteCount];
            if (byteCount > 0)
            {
                Marshal.Copy(bytes, array, 0, (int)byteCount);
            }
            return new List<byte>(array);
        }
    
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
