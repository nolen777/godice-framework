// GoDiceDll.cpp
//

#include "stdafx.h"

#include "GoDiceDll.h"

#include <ppltasks.h>
#include <future>

#pragma comment(lib, "windowsapp")

// macro for file, see also https://stackoverflow.com/a/14421702
#define __WFILE__ L"BleWinrtDll.cpp"

using namespace winrt;

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;

using namespace Windows::Storage::Streams;

static BluetoothLEAdvertisementWatcher gWatcher = nullptr;

struct DeviceSession;

static GDDeviceFoundCallbackFunction gDeviceFoundCallback = nullptr;
static GDDataCallbackFunction gDataReceivedCallback = nullptr;
static GDDeviceConnectedCallbackFunction gDeviceConnectedCallback = nullptr;
static GDDeviceDisconnectedCallbackFunction gDeviceDisconnectedCallback = nullptr;
static GDListenerStoppedCallbackFunction gListenerStoppedCallback = nullptr;
static GDLogger gLogger = nullptr;

using std::condition_variable;
using std::exception;
using std::shared_future;
using std::map;
using std::mutex;
using std::scoped_lock;
using std::shared_ptr;
using std::string;
using std::unique_lock;

static mutex gMapMutex;
static map<string, shared_ptr<DeviceSession>> gDevicesByIdentifier;

static inline constexpr guid kServiceGuid = guid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kWriteGuid = guid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kNotifyGuid = guid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

static void Log(const char* str)
{
    if (gLogger)
    {
        gLogger(str);
    }
}

template <class Args>
static void Log(const char* str, Args&& args...)
{
    if (gLogger)
    {
        auto formattedStr = std::vformat(str, std::make_format_args(args));
        gLogger(formattedStr.c_str());
    }
}

static void ReceivedEvent(const BluetoothLEAdvertisementWatcher& watcher,
                          const BluetoothLEAdvertisementReceivedEventArgs& args);
static void internalConnect(const string& identifier);
static void internalSend(const char* id, uint32_t data_size, uint8_t* data);
static void internalConnectionChangedHandler(const BluetoothLEDevice& dev, const string& identifier);

struct DeviceSession
{
private:
    const BluetoothLEDevice device_;
    const string identifier_;
    const string name_;
    GattSession session_ = nullptr;
    GattDeviceService service_ = nullptr;
    GattCharacteristic write_characteristic_ = nullptr;
    GattCharacteristic notify_characteristic_ = nullptr;

public:
    static shared_ptr<DeviceSession> MakeSession(uint64_t bluetoothAddr)
    {
        auto device = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddr).get();
        GattSession session = GattSession::FromDeviceIdAsync(device.BluetoothDeviceId()).get();
        GattDeviceService service = device.GetGattServicesForUuidAsync(kServiceGuid).get().Services().GetAt(0);
        GattCharacteristic notifyCh = service.GetCharacteristicsForUuidAsync(kNotifyGuid).get().Characteristics().
                                              GetAt(0);
        GattCharacteristic writeCh = service.GetCharacteristicsForUuidAsync(kWriteGuid).get().Characteristics().
                                             GetAt(0);
        return std::make_shared<DeviceSession>(device, std::to_string(bluetoothAddr), session, service, notifyCh,
                                               writeCh);
    }

    DeviceSession(
        const BluetoothLEDevice& dev,
        const string& ident,
        const GattSession& session,
        const GattDeviceService& service,
        const GattCharacteristic& notifyCh,
        const GattCharacteristic& writeCh
    ) : device_(dev), identifier_(ident), name_(to_string(dev.Name())), session_(session), service_(service),
        notify_characteristic_(notifyCh), write_characteristic_(writeCh)
    {
    }

    void Connect() const
    {
        device_.ConnectionStatusChanged([this](auto&& dev, auto&& result)
        {
            internalConnectionChangedHandler(dev, identifier_);
        });

        session_.MaintainConnection(true);

        notify_characteristic_.ValueChanged([=](auto&& ch, auto&& args)
        {
            if (gDataReceivedCallback != nullptr)
            {
                const IBuffer& data = args.CharacteristicValue();
                gDataReceivedCallback(identifier_.c_str(), data.Length(), data.data());
            }
        });
        notify_characteristic_.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify);

        if (gDeviceConnectedCallback)
        {
            gDeviceConnectedCallback(identifier_.c_str());
        }
    }

    void Disconnect() const
    {
        notify_characteristic_.ValueChanged([](auto&& ch, auto&& args)
        {
        });
        device_.ConnectionStatusChanged([](auto&& ch, auto&& args)
        {
        });

        session_.MaintainConnection(false);
    }

    void SetNotifyCharacteristic(const GattCharacteristic& nCh)
    {
        notify_characteristic_ = nCh;
    }

    void SetWriteCharacteristic(const GattCharacteristic& wCh)
    {
        write_characteristic_ = wCh;
    }

    const string& DeviceName() const { return name_; }

    auto GetWriteCharacteristic() const -> const GattCharacteristic&
    {
        return write_characteristic_;
    }

    ~DeviceSession()
    {
        Disconnect();
    }
};

void godice_set_callbacks(
    GDDeviceFoundCallbackFunction deviceFoundCallback,
    GDDataCallbackFunction dataReceivedCallback,
    GDDeviceConnectedCallbackFunction deviceConnectedCallback,
    GDDeviceDisconnectedCallbackFunction deviceDisconnectedCallback,
    GDListenerStoppedCallbackFunction listenerStoppedCallback)
{
    gDeviceFoundCallback = deviceFoundCallback;
    gDataReceivedCallback = dataReceivedCallback;
    gDeviceConnectedCallback = deviceConnectedCallback;
    gDeviceDisconnectedCallback = deviceDisconnectedCallback;
    gListenerStoppedCallback = listenerStoppedCallback;
}

void godice_set_logger(GDLogger logger)
{
    gLogger = logger;
}

void godice_start_listening()
{
    if (gWatcher == nullptr)
    {
        gWatcher = BluetoothLEAdvertisementWatcher();
    }
    gWatcher.ScanningMode(BluetoothLEScanningMode::Active);
    gWatcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(kServiceGuid);

    auto result = gWatcher.Received(ReceivedEvent);

    gWatcher.Stopped([=](auto&&, auto&&)
    {
        Log("Watcher Stopped");
        godice_stop_listening();
    });

    gWatcher.Start();
}

void godice_connect(const char* identifier)
{
    string strIdent = identifier;
    std::thread([strIdent]()
    {
        internalConnect(strIdent);
    }).detach();
}

void godice_disconnect(const char* identifier)
{
    gDevicesByIdentifier[identifier]->Disconnect();
}

void godice_send(const char* identifier, uint32_t data_size, uint8_t* data)
{
    internalSend(identifier, data_size, data);
}

static void internalSend(const char* id, uint32_t data_size, uint8_t* data)
{
    const auto& session = gDevicesByIdentifier[id];

    const DataWriter writer;
    writer.WriteBytes(winrt::array_view(data, data + data_size));

    const IBuffer buf = writer.DetachBuffer();

    auto status = session->GetWriteCharacteristic().WriteValueAsync(buf, GattWriteOption::WriteWithoutResponse).get();
    Log("Wrote data with status of {}\n", (int)status);
}

static void internalConnectionChangedHandler(const BluetoothLEDevice& dev, const string& identifier)
{
    if (dev.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
    {
        if (gDeviceDisconnectedCallback)
        {
            gDeviceDisconnectedCallback(identifier.c_str());
        }
    }
}

static void internalConnect(const string& identifier)
{
    scoped_lock lk(gMapMutex);
    const auto& device = gDevicesByIdentifier[identifier];
    device->Connect();
}

static void ReceivedEvent(const BluetoothLEAdvertisementWatcher& watcher,
                          const BluetoothLEAdvertisementReceivedEventArgs& args)
{
    uint64_t btAddr = args.BluetoothAddress();
    string identifier = std::to_string(btAddr);
    {
        std::scoped_lock lock(gMapMutex);
        if (gDevicesByIdentifier.count(identifier) == 0)
        {
            gDevicesByIdentifier.emplace(std::make_pair(identifier, DeviceSession::MakeSession(btAddr)));
        }

        if (gDeviceFoundCallback)
        {
            const auto& device = gDevicesByIdentifier[identifier];
            gDeviceFoundCallback(identifier.c_str(), device->DeviceName().c_str());
        }
    }
}

void godice_stop_listening()
{
    const std::scoped_lock lock(gMapMutex);
    gWatcher.Stop();
}
