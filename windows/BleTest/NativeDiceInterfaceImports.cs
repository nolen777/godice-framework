using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace UnityGoDiceInterface {
    public class NativeDiceInterfaceImports : IDiceInterfaceImports {
        private const string BundleName = "BleWinrtDll.dll";
        
        private delegate void MonoDelegateDataMessage(UInt64 addr, UInt32 byteCount, IntPtr bytePtr);
        private delegate void MonoDelegateFoundMessage(UInt64 addr, string name);

        [DllImport (dllName: BundleName, EntryPoint = "godice_start_listening")]
        private static extern void _NativeBridgeStartListening();
  
        [DllImport (dllName: BundleName, EntryPoint = "godice_stop_listening")]
        private static extern void _NativeBridgeStopListening();
  
        [DllImport (dllName: BundleName, EntryPoint = "godice_set_callbacks")]
        private static extern void _NativeBridgeSetCallbacks(MonoDelegateFoundMessage deviceFound, MonoDelegateDataMessage dataReceived);

        [DllImport(dllName: BundleName, EntryPoint = "godice_connect")]
        private static extern void _NativeBridgeConnect(UInt64 addr);

        private static List<byte> BytesFromRawPointer(UInt32 byteCount, IntPtr bytes) {
            byte[] array = new byte[byteCount];
            if (byteCount > 0)
            {
                Marshal.Copy(bytes, array, 0, (int)byteCount);
            }
            return new List<byte>(array);
        }
        private static void MonoDelegateDeviceFound(UInt64 addr, string name)
        {
            if (IDiceInterfaceImports.DeviceFound != null)
            {
                IDiceInterfaceImports.DeviceFound(addr, name);
            }
        }

        private static void MonoDelegateMessageReceived(UInt64 addr, UInt32 byteCount, IntPtr bytePtr) {
            if (IDiceInterfaceImports.DataReceived != null) {
                IDiceInterfaceImports.DataReceived(addr, BytesFromRawPointer(byteCount, bytePtr));
            }
        }
        
        public void StartListening() {
            _NativeBridgeSetCallbacks(MonoDelegateDeviceFound, MonoDelegateMessageReceived);
            _NativeBridgeStartListening();
        }
        
        public void StopListening() {
            _NativeBridgeStopListening();
        }

        public void Connect(UInt64 addr)
        {
            _NativeBridgeConnect(addr);
        }
    }
}
