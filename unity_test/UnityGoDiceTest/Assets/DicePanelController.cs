using System.Collections;
using System.Collections.Generic;
using System.Threading;
using TMPro;
using UnityEngine;

public class DicePanelController : MonoBehaviour {
    public TMP_Text mainText;
    private DiceInterface diceInterface;
    
    struct DiceRoll {
        public string name;
        public byte x;
        public byte y;
        public byte z;
    }
    
    private List<DiceRoll> _diceRolls = new List<DiceRoll>();
    
    // Start is called before the first frame update
    void Start() {
        diceInterface = GetComponent<DiceInterface>();
        mainText.text = "Hello, World!\n";

        diceInterface.callback = ReceiveRollCallback;
        diceInterface.StartListening();
    }

    void ReceiveRollCallback(string name, byte[] bytes) {
        lock (_diceRolls) {
            Debug.Log($"Received roll callback: {name} {bytes.Length} bytes\n");
            _diceRolls.Add(new DiceRoll { name = name, x = 0, y = 0, z = 0 });
        }
    }

    // Update is called once per frame
    void Update() {
        List<DiceRoll> toProcess = new List<DiceRoll>();
        lock (_diceRolls) {
            if (_diceRolls.Count > 0) {
                (_diceRolls, toProcess) = (toProcess, _diceRolls);
            }
        }
        
        foreach (DiceRoll dr in toProcess) {
             mainText.text += $"{dr.name} rolled {dr.x} {dr.y} {dr.z}\n";
        }
    }
}
