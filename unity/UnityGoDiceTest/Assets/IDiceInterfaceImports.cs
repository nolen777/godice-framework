using System.Collections.Generic;

namespace UnityGoDiceInterface {
    public interface IDiceInterfaceImports {
        delegate void DelegateMessage(string name, List<byte> bytes);

        public void StartListening();

        public void StopListening();

        public void SetCallback(DelegateMessage delegateMessage);
    }
}
