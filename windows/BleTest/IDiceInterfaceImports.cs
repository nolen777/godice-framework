using System.Collections.Generic;

namespace UnityGoDiceInterface {
    public interface IDiceInterfaceImports {
        delegate void DeviceFoundDelegate(UInt64 addr, string name);
        delegate void DataReceivedDelegate(UInt64 addr, List<byte> bytes);
        
        protected static DeviceFoundDelegate DeviceFound = (addr, name) => { Console.Out.WriteLine($"Found device {addr} {name}"); };
        protected static DataReceivedDelegate DataReceived = (addr, bytes) => { Console.Out.WriteLine($"Received bytes for {addr}"); };
        
        public void StartListening();
  
        public void StopListening();

        public void SetCallbacks(IDiceInterfaceImports.DeviceFoundDelegate deviceFound, IDiceInterfaceImports.DataReceivedDelegate dataReceived) {
            DeviceFound = deviceFound;
            DataReceived = dataReceived;
        }

        public void Connect(UInt64 addr);
    }
}