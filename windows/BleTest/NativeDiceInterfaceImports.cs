using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace UnityGoDiceInterface {
    public class NativeDiceInterfaceImports : IDiceInterfaceImports {
        private const string BundleName = "BleWinrtDll.dll";
        
        private delegate void MonoDelegateDataMessage(string identifier, UInt32 byteCount, IntPtr bytePtr);
        private delegate void MonoDelegateFoundMessage(string identifier, string name);

        [DllImport (dllName: BundleName, EntryPoint = "godice_start_listening")]
        private static extern void _NativeBridgeStartListening();
  
        [DllImport (dllName: BundleName, EntryPoint = "godice_stop_listening")]
        private static extern void _NativeBridgeStopListening();
  
        [DllImport (dllName: BundleName, EntryPoint = "godice_set_callbacks")]
        private static extern void _NativeBridgeSetCallbacks(MonoDelegateFoundMessage deviceFound, MonoDelegateDataMessage dataReceived);

        [DllImport(dllName: BundleName, EntryPoint = "godice_connect")]
        private static extern void _NativeBridgeConnect(string identifier);

        private static List<byte> BytesFromRawPointer(UInt32 byteCount, IntPtr bytes) {
            byte[] array = new byte[byteCount];
            if (byteCount > 0)
            {
                Marshal.Copy(bytes, array, 0, (int)byteCount);
            }
            return new List<byte>(array);
        }
        private static void MonoDelegateDeviceFound(string identifier, string name)
        {
            if (IDiceInterfaceImports.DeviceFound != null)
            {
                IDiceInterfaceImports.DeviceFound(identifier, name);
            }
        }

        private static void MonoDelegateMessageReceived(string identifier, UInt32 byteCount, IntPtr bytePtr) {
            if (IDiceInterfaceImports.DataReceived != null) {
                IDiceInterfaceImports.DataReceived(identifier, BytesFromRawPointer(byteCount, bytePtr));
            }
        }
        
        public void StartListening() {
            _NativeBridgeSetCallbacks(MonoDelegateDeviceFound, MonoDelegateMessageReceived);
            _NativeBridgeStartListening();
        }
        
        public void StopListening() {
            _NativeBridgeStopListening();
        }

        public void Connect(string identifier)
        {
            _NativeBridgeConnect(identifier);
        }
    }
}
