#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN

using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Windows.Devices.Bluetooth.Advertisement;

public class WindowsBluetoothWatcher : MonoBehaviour
{
    private BluetoothLEAdvertisementWatcher _watcher;

    void Start()
    {
        _watcher = new BluetoothLEAdvertisementWatcher();
        _watcher.Received += WatcherOnReceived;
        _watcher.Start();
    }

    //This method should be called when a beacon is detected
    void WatcherOnReceived(BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementReceivedEventArgs args)
    {
        //Just a simple check if this method is even called
        Debug.Log("Beacon detected!");
    }

    // Update is called once per frame
    void Update()
    {
        
    }
}

#endif
