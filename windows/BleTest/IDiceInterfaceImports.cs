using System.Collections.Generic;

namespace UnityGoDiceInterface {
    public interface IDiceInterfaceImports {
        delegate void DeviceFoundDelegate(string identifier, string name);
        delegate void DataReceivedDelegate(string identifier, List<byte> bytes);
        
        protected static DeviceFoundDelegate DeviceFound = (identifier, name) => { Console.Out.WriteLine($"Found device {identifier} {name}"); };
        protected static DataReceivedDelegate DataReceived = (identifier, bytes) => { Console.Out.WriteLine($"Received bytes for {identifier}"); };
        
        public void StartListening();
  
        public void StopListening();

        public void SetCallbacks(IDiceInterfaceImports.DeviceFoundDelegate deviceFound, IDiceInterfaceImports.DataReceivedDelegate dataReceived) {
            DeviceFound = deviceFound;
            DataReceived = dataReceived;
        }

        public void Connect(string identifier);
    }
}