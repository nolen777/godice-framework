using System.Collections.Generic;

namespace UnityGoDiceInterface {
    public enum DiceConnectionState : uint {
        Unknown = 0,
        Discovered = 1,
        Connecting = 2,
        Subscribing = 3,
        Ready = 4,
        Disconnecting = 5,
        Disconnected = 6,
        RetryWait = 7,
    }

    public enum DiceConnectionReason : uint {
        None = 0,
        Requested = 1,
        LinkLoss = 2,
        ConnectionFailed = 3,
        ProtocolError = 4,
        AdapterUnavailable = 5,
        Reset = 6,
    }

    public readonly struct DiceConnectionEvent {
        public readonly string Identifier;
        public readonly string Name;
        public readonly DiceConnectionState State;
        public readonly DiceConnectionReason Reason;
        public readonly int NativeStatus;
        public readonly ulong MonotonicMilliseconds;
        public readonly string Detail;

        public DiceConnectionEvent(string identifier,
                                   string name,
                                   DiceConnectionState state,
                                   DiceConnectionReason reason,
                                   int nativeStatus,
                                   ulong monotonicMilliseconds,
                                   string detail) {
            Identifier = identifier;
            Name = name;
            State = state;
            Reason = reason;
            NativeStatus = nativeStatus;
            MonotonicMilliseconds = monotonicMilliseconds;
            Detail = detail;
        }
    }

    public interface IDiceInterfaceImports {
        delegate void DelegateMessage(string name, List<byte> bytes);
        delegate void DelegateStateChange(DiceConnectionEvent connectionEvent);

        public void StartListening();

        public void StopListening();

        public void SetCallback(DelegateMessage delegateMessage);

        public void SetStateCallback(DelegateStateChange delegateStateChange);
    }
}
