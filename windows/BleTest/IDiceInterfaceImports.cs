using System.Collections.Generic;

namespace UnityGoDiceInterface {
    public interface IDiceInterfaceImports {
        delegate void DelegateMessage(string name, List<byte> bytes);
        
        protected static DelegateMessage Delegate;
        
        public void StartListening();
  
        public void StopListening();
        
        public void SetCallback(IDiceInterfaceImports.DelegateMessage delegateMessage) {
            Delegate = delegateMessage;
        }
    }
}