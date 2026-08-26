using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using AOT;
using UnityEngine;

namespace UnityGoDiceInterface {
    public class NativeDiceInterfaceImports : IDiceInterfaceImports {
#if UNITY_IOS
        private const string BundleName = "__Internal";
#elif UNITY_STANDALONE_OSX || UNITY_EDITOR_OSX
        private const string BundleName = "DarwinGodiceBundle";
#elif UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        private const string BundleName = "GoDiceDll";
#else
        private const string BundleName = "GoDiceUnsupported";
#endif

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void DeviceFoundCallback(IntPtr identifier, IntPtr name);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void DataCallback(IntPtr identifier, UInt32 byteCount, IntPtr bytes);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void DeviceCallback(IntPtr identifier);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void ListenerStoppedCallback();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void LoggerCallback(IntPtr message);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void DeviceStateCallback(
            IntPtr identifier,
            UInt32 state,
            UInt32 reason,
            Int32 nativeStatus,
            UInt64 monotonicMilliseconds,
            IntPtr detail);

        private static readonly DeviceFoundCallback DeviceFoundCallbackInstance = DeviceFound;
        private static readonly DataCallback DataCallbackInstance = DataReceived;
        private static readonly DeviceCallback DeviceConnectedCallbackInstance = DeviceConnected;
        private static readonly DeviceCallback DeviceConnectionFailedCallbackInstance = DeviceConnectionFailed;
        private static readonly DeviceCallback DeviceDisconnectedCallbackInstance = DeviceDisconnected;
        private static readonly ListenerStoppedCallback ListenerStoppedCallbackInstance = ListenerStopped;
        private static readonly LoggerCallback LoggerCallbackInstance = Log;
        private static readonly DeviceStateCallback DeviceStateCallbackInstance = DeviceStateChanged;

        private static readonly object StateLock = new object();
        private static readonly Dictionary<string, string> NamesByIdentifier = new Dictionary<string, string>();
        private static readonly HashSet<string> ConnectingIdentifiers = new HashSet<string>();
        private static readonly HashSet<string> ConnectedIdentifiers = new HashSet<string>();

        private static IDiceInterfaceImports.DelegateMessage _delegate;
        private static IDiceInterfaceImports.DelegateStateChange _stateDelegate;

        [DllImport(BundleName, EntryPoint = "godice_set_callbacks", CallingConvention = CallingConvention.Cdecl)]
        private static extern void NativeSetCallbacks(
            DeviceFoundCallback deviceFoundCallback,
            DataCallback dataCallback,
            DeviceCallback deviceConnectedCallback,
            DeviceCallback deviceConnectionFailedCallback,
            DeviceCallback deviceDisconnectedCallback,
            ListenerStoppedCallback listenerStoppedCallback);

        [DllImport(BundleName, EntryPoint = "godice_set_device_state_callback", CallingConvention = CallingConvention.Cdecl)]
        private static extern void NativeSetDeviceStateCallback(DeviceStateCallback deviceStateCallback);

        [DllImport(BundleName, EntryPoint = "godice_set_logger", CallingConvention = CallingConvention.Cdecl)]
        private static extern void NativeSetLogger(LoggerCallback logger);

        [DllImport(BundleName, EntryPoint = "godice_start_listening", CallingConvention = CallingConvention.Cdecl)]
        private static extern void NativeStartListening();

        [DllImport(BundleName, EntryPoint = "godice_stop_listening", CallingConvention = CallingConvention.Cdecl)]
        private static extern void NativeStopListening();

        [DllImport(BundleName, EntryPoint = "godice_connect", CallingConvention = CallingConvention.Cdecl)]
        private static extern void NativeConnect(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string identifier);

        private static string StringFromUtf8(IntPtr pointer) {
            if (pointer == IntPtr.Zero) {
                return string.Empty;
            }

            var length = 0;
            while (Marshal.ReadByte(pointer, length) != 0) {
                length++;
            }

            var bytes = new byte[length];
            if (length > 0) {
                Marshal.Copy(pointer, bytes, 0, length);
            }
            return Encoding.UTF8.GetString(bytes);
        }

        private static List<byte> BytesFromRawPointer(UInt32 byteCount, IntPtr bytes) {
            if (byteCount > int.MaxValue) {
                Debug.LogError($"GoDice returned an invalid payload size: {byteCount}");
                return new List<byte>();
            }

            var array = new byte[(int)byteCount];
            if (array.Length > 0) {
                if (bytes == IntPtr.Zero) {
                    Debug.LogError("GoDice returned a null payload");
                    return new List<byte>();
                }
                Marshal.Copy(bytes, array, 0, array.Length);
            }
            return new List<byte>(array);
        }

        private static string NameForIdentifier(string identifier) {
            lock (StateLock) {
                return NamesByIdentifier.TryGetValue(identifier, out var name) && !string.IsNullOrEmpty(name)
                    ? name
                    : identifier;
            }
        }

        [MonoPInvokeCallback(typeof(DeviceFoundCallback))]
        private static void DeviceFound(IntPtr identifierPointer, IntPtr namePointer) {
            var identifier = StringFromUtf8(identifierPointer);
            var name = StringFromUtf8(namePointer);

            lock (StateLock) {
                NamesByIdentifier[identifier] = name;
                if (ConnectingIdentifiers.Contains(identifier) || ConnectedIdentifiers.Contains(identifier)) {
                    return;
                }
                ConnectingIdentifiers.Add(identifier);
            }

            NativeConnect(identifier);
        }

        [MonoPInvokeCallback(typeof(DataCallback))]
        private static void DataReceived(IntPtr identifierPointer, UInt32 byteCount, IntPtr bytes) {
            var identifier = StringFromUtf8(identifierPointer);
            _delegate?.Invoke(NameForIdentifier(identifier), BytesFromRawPointer(byteCount, bytes));
        }

        [MonoPInvokeCallback(typeof(DeviceCallback))]
        private static void DeviceConnected(IntPtr identifierPointer) {
            var identifier = StringFromUtf8(identifierPointer);
            lock (StateLock) {
                ConnectingIdentifiers.Remove(identifier);
                ConnectedIdentifiers.Add(identifier);
            }
            _delegate?.Invoke(NameForIdentifier(identifier), new List<byte>());
        }

        [MonoPInvokeCallback(typeof(DeviceCallback))]
        private static void DeviceConnectionFailed(IntPtr identifierPointer) {
            var identifier = StringFromUtf8(identifierPointer);
            lock (StateLock) {
                ConnectingIdentifiers.Remove(identifier);
                ConnectedIdentifiers.Remove(identifier);
            }
            Debug.LogWarning($"GoDice connection failed: {NameForIdentifier(identifier)}");
        }

        [MonoPInvokeCallback(typeof(DeviceCallback))]
        private static void DeviceDisconnected(IntPtr identifierPointer) {
            var identifier = StringFromUtf8(identifierPointer);
            lock (StateLock) {
                ConnectingIdentifiers.Remove(identifier);
                ConnectedIdentifiers.Remove(identifier);
            }
            Debug.Log($"GoDice disconnected: {NameForIdentifier(identifier)}");
        }

        [MonoPInvokeCallback(typeof(ListenerStoppedCallback))]
        private static void ListenerStopped() {
            Debug.Log("GoDice listener stopped");
        }

        [MonoPInvokeCallback(typeof(DeviceStateCallback))]
        private static void DeviceStateChanged(IntPtr identifierPointer,
                                               UInt32 state,
                                               UInt32 reason,
                                               Int32 nativeStatus,
                                               UInt64 monotonicMilliseconds,
                                               IntPtr detailPointer) {
            var identifier = StringFromUtf8(identifierPointer);
            _stateDelegate?.Invoke(new DiceConnectionEvent(
                identifier,
                NameForIdentifier(identifier),
                (DiceConnectionState)state,
                (DiceConnectionReason)reason,
                nativeStatus,
                monotonicMilliseconds,
                StringFromUtf8(detailPointer)));
        }

        [MonoPInvokeCallback(typeof(LoggerCallback))]
        private static void Log(IntPtr message) {
            Debug.Log(StringFromUtf8(message));
        }

        public void SetCallback(IDiceInterfaceImports.DelegateMessage delegateMessage) {
            _delegate = delegateMessage;
        }

        public void SetStateCallback(IDiceInterfaceImports.DelegateStateChange delegateStateChange) {
            _stateDelegate = delegateStateChange;
        }

        public void StartListening() {
            NativeSetLogger(LoggerCallbackInstance);
            NativeSetCallbacks(
                DeviceFoundCallbackInstance,
                DataCallbackInstance,
                DeviceConnectedCallbackInstance,
                DeviceConnectionFailedCallbackInstance,
                DeviceDisconnectedCallbackInstance,
                ListenerStoppedCallbackInstance);
            try {
                NativeSetDeviceStateCallback(DeviceStateCallbackInstance);
            }
            catch (EntryPointNotFoundException) {
                Debug.LogWarning("This GoDice native plugin does not provide connection diagnostics");
            }
            NativeStartListening();
        }

        public void StopListening() {
            NativeStopListening();
        }
    }
}
