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

## Unity sample

The sample project is `unity/UnityGoDiceTest` and was created with Unity
2022.3.12f1. Install the native plugin for the current host platform before
opening the sample scene.

The managed bridge scans for GoDice devices, connects each discovered device,
and copies native callback data before queuing it for processing on Unity's main
thread. Native callbacks may arrive on a background thread; application code
must not call Unity APIs directly from those callbacks.

## Current status

The macOS native schemes and Unity script compilation are verified. Windows
hardware stability work is the next milestone; see the implementation plan for
the connection-state, lifecycle, and soak-test work still to be completed.
