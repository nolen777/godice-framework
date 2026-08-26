# GoDice Framework Implementation Plan

The immediate goal is to make the macOS, Windows, and Unity samples in this
repository exercise the same behavior. Packaging this repository for import by
other Unity projects is intentionally out of scope for now.

Progress:

- [x] Restore a consistent sample baseline.
- [x] Add an observable connection model.
- [ ] Replace the Windows session lifecycle.
- [ ] Harden the macOS lifecycle.
- [ ] Extract the GoDice protocol.
- [ ] Add automated verification.
- [ ] Prove Windows stability on hardware.

## 1. Restore a consistent sample baseline

- Use the existing multi-callback C ABI on both native platforms and in Unity.
- Make native library names and exported entry points agree with the install
  scripts.
- Connect discovered dice from the Unity sample and marshal native callbacks to
  Unity's main thread.
- Report connection success only after notification subscription is ready.
- Wire every declared callback, reject sends before a device is writable, and
  make sample packet parsing bounds-safe.
- Build the macOS bundle, framework, CLI, and app. Compile the Unity scripts.

Windows compilation remains a Windows-hosted verification because Bazel only
packages the checked-in DLL; it does not build the C++ source.

## 2. Add an observable connection model

- Define explicit per-device states: discovered, connecting, subscribing,
  ready, disconnecting, disconnected, and retry-wait.
- Add structured diagnostic events with timestamps and native error/status
  codes.
- Distinguish an intentional disconnect from link loss.
- Add a small diagnostic view to both native sample apps.

## 3. Replace the Windows session lifecycle

- Remove blocking `.get()` calls from the Bluetooth work queue.
- Replace the global semaphore with serialized, per-device asynchronous state.
- Keep the device, GATT session, service, and characteristics alive while the
  device is ready.
- Store and revoke every WinRT event token deterministically.
- Await notification subscription and the first notification before declaring
  the die ready.
- Implement bounded reconnect with backoff and an explicit cancellation path.
- Shut down without asynchronous work referring to a destroyed session.

## 4. Harden the macOS lifecycle

- Apply the same states and event semantics to CoreBluetooth.
- Serialize controller state on its delegate queue.
- Handle discovery, subscription, connection, and disconnection errors
  consistently.
- Match Windows reconnect and intentional-disconnect behavior.

## 5. Extract the GoDice protocol

- Implement bounds-safe readers for all known responses: roll, stable variants,
  battery, color, charging, tap, and double-tap.
- Implement typed writers for initialization, sensitivity, LEDs, battery,
  color, tap settings, and roll-detection settings.
- Send initialization only after the connection is ready.
- Keep transport behavior independent from dice-message interpretation.

## 6. Add automated verification

- Unit-test protocol parsing and message construction with malformed and partial
  payloads.
- Test managed callback marshalling with a fake native adapter.
- Add ABI/export checks for both native binaries.
- Add repeatable build checks for the macOS native samples, Windows DLL/CLI, and
  Unity sample scripts.

## 7. Prove Windows stability on hardware

- Record disconnect reason, GATT/session status, reconnect attempt, and recovery
  time.
- Run single-die and multi-die soak tests for several hours.
- Exercise radio toggles, die sleep/wake, out-of-range recovery, application
  pause/resume, and clean shutdown.
- Tune retry timing only after the diagnostic data identifies the actual failure
  mode.
