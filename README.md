# GoDice Framework

Cross-platform native Bluetooth support for using GoDice hardware from Unity.
The repository contains a CoreBluetooth implementation for macOS, a C++/WinRT
implementation for Windows, native command-line samples, and a Unity sample.

The current development plan is in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md).

## macOS samples

The Xcode project contains four schemes:

- `DarwinGodiceBundle`: the native bundle loaded by Unity.
- `GodiceClient`: the Swift framework.
- `godice_c_test`: a native C command-line client.
- `GodiceClientApp`: a small SwiftUI client.

Build one from the command line with:

```sh
xcodebuild \
  -project godice_client.xcodeproj \
  -scheme DarwinGodiceBundle \
  -configuration Debug \
  CODE_SIGNING_ALLOWED=NO \
  build
```

Install a release build of the bundle into the Unity sample with:

```sh
./scripts/install_darwin_bundle.sh
```

## Windows samples

Open `windows/GoDiceInterface.sln` in Visual Studio 2022 and build the
`GoDiceDll` and `GoDiceConsoleApp` projects with the v143 toolset. The Unity
sample loads the library as `GoDiceDll`.

Bazel currently packages the checked-in Windows DLL; it does not compile the
C++/WinRT source. After rebuilding that DLL on Windows, copy the checked-in
artifact into the Unity sample with:

```sh
./scripts/install_windows_dll.sh
```

The Windows transport serializes operations independently for each die and does
not block its Bluetooth coordination queue on WinRT operations. A connection is
reported ready only after both characteristics are available and notification
subscription is acknowledged. Link loss triggers
up to four reconnect attempts with 0.5, 1, 2, and 4 second delays; an explicit
disconnect or reset cancels pending setup and retry work.

## Unity sample

The sample project is `unity/UnityGoDiceTest` and was created with Unity
2022.3.12f1. Install the native plugin for the current host platform before
opening the sample scene.

The managed bridge scans for GoDice devices, connects each discovered device,
and copies native callback data before queuing it for processing on Unity's main
thread. Native callbacks may arrive on a background thread; application code
must not call Unity APIs directly from those callbacks.

## Connection diagnostics

The optional `godice_set_device_state_callback` ABI reports a per-device state
transition with a monotonic millisecond timestamp, native status code, reason,
and detail message. The states are:

- `discovered`
- `connecting`
- `subscribing`
- `ready`
- `disconnecting`
- `disconnected`
- `retry-wait`

Disconnect reasons distinguish an application request, link loss, connection
failure, protocol error, unavailable adapter, and controller reset. Callback
strings are valid only for the duration of the callback and must be copied by
the receiver.

The Unity bridge detects older native libraries that do not export this
optional function, allowing projects to report a clear warning instead of
failing during startup.

## Current status

The macOS native schemes, connection diagnostics, Unity script compilation, and
Windows x64 native samples are build-verified. Hardening the macOS lifecycle is
the next implementation milestone; Windows hardware soak testing remains
required before claiming long-duration connection stability.
