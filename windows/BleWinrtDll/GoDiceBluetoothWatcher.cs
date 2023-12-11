using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

class GoDiceBluetoothWatcher
{
    private BluetoothLEAdvertisementWatcher _watcher;
    private static readonly Guid _serviceGuid = Guid.Parse("6e400001-b5a3-f393-e0a9-e50e24dcca9e");

    public delegate void DataDelegate(string name, List<byte> bytes);

    private DataDelegate _dataCallback;
    
    public void SetDataCallback(DataDelegate newCallback)
    {
        _dataCallback = newCallback;
    }
    
    public GoDiceBluetoothWatcher(DataDelegate callback)
    {
        _dataCallback = callback;
        _watcher = new BluetoothLEAdvertisementWatcher
        {
            ScanningMode = BluetoothLEScanningMode.Active
        };
        _watcher.AdvertisementFilter.Advertisement.ServiceUuids.Add(_serviceGuid);
        
        _watcher.Received += OnAdvertisementReceived;
        _watcher.Stopped += OnStopped;
    }

    public void Start()
    {
        StartDeviceScan();
        _watcher.Start();
    }

    public void Stop()
    {
        _watcher.Stop();
    }

    private Dictionary<String, DiceSession> _sessions = new Dictionary<string, DiceSession>();
    
    private async void OnAdvertisementReceived(BluetoothLEAdvertisementWatcher watcher,
        BluetoothLEAdvertisementReceivedEventArgs eventArgs)
    {
        var type = eventArgs.AdvertisementType;
        var ad = eventArgs.Advertisement;

        var uuids = ad.ServiceUuids;

        var device = await BluetoothLEDevice.FromBluetoothAddressAsync(eventArgs.BluetoothAddress);
        _sessions[device.Name] = await DiceSession.NewSession(device, ((leDevice, data) =>
                {
                    var byteArray = new byte[data.Length];
                    data.CopyTo(byteArray);
                    _dataCallback(device.Name, new List<byte>(byteArray));
                }
            ));
    }

    class DiceSession
    {
        private BluetoothLEDevice _device;
        private InternalCallback _callback;

        private GattCharacteristic _writeCharacteristic;
        private GattCharacteristic _notifyCharacteristic;

        private static readonly Guid _writeGuid = Guid.Parse("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
        private static readonly Guid _notifyGuid = Guid.Parse("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

        public delegate void InternalCallback(BluetoothLEDevice device, IBuffer data);

        public async static Task<DiceSession> NewSession(BluetoothLEDevice device, InternalCallback callback)
        {
            var services = await device.GetGattServicesAsync();
            //var services = await device.GetGattServicesForUuidAsync(_serviceGuid);
            var service = services.Services.First(x => x.Uuid == _serviceGuid);
            
            var wCh = (await service.GetCharacteristicsForUuidAsync(_writeGuid)).Characteristics.First();
            var nCh = (await service.GetCharacteristicsForUuidAsync(_notifyGuid)).Characteristics.First();

            GattCommunicationStatus status = await nCh.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify);
            
            if (status == GattCommunicationStatus.Success)
            {
                return new DiceSession(device, callback, wCh, nCh);
            }
            else
            {
                throw new ArgumentException($"error subscribing to notifications: {status}");
            }
        }
        
        private DiceSession(BluetoothLEDevice device, InternalCallback callback, GattCharacteristic writeCharacteristic, GattCharacteristic notifyCharacteristic)
        {
            _device = device;
            _callback = callback;
            _writeCharacteristic = writeCharacteristic;
            _notifyCharacteristic = notifyCharacteristic;

            _notifyCharacteristic.ValueChanged += OnValueChanged;
        }
        
        private async void OnValueChanged(GattCharacteristic ch, GattValueChangedEventArgs args)
        {
            _callback(_device, args.CharacteristicValue);
        }
    }

    private async void OnStopped(BluetoothLEAdvertisementWatcher watcher,
        BluetoothLEAdvertisementWatcherStoppedEventArgs stoppedEventArgs)
    {
        Debug.WriteLine($"Stopped with args {stoppedEventArgs}");
    }
}