// GoDiceDll.cpp
//

#include "GoDiceDll.h"

#include "stdafx.h"

#include "GoDiceDll.h"

#include <ppltasks.h>
#include <future>
#include <unordered_set>

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
using std::mutex;
using std::scoped_lock;
using std::shared_ptr;
using std::string;
using std::unique_lock;
using std::unordered_map;
using std::pmr::unordered_set;

static mutex gMapMutex;
static unordered_map<string, shared_ptr<DeviceSession>> gDevicesByIdentifier;
static unordered_set<string> gDevicesInProgress;

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

static void ReceivedDeviceFoundEvent(const BluetoothLEAdvertisementWatcher& watcher,
                          const BluetoothLEAdvertisementReceivedEventArgs& args);
static void internalConnect(const string& identifier);
static void internalSend(const string& identifier, const IBuffer& buffer);
static void internalConnectionChangedHandler(const BluetoothLEDevice& dev, const string& identifier);

struct DeviceSession
{
private:
    const BluetoothLEDevice device_;
    const string identifier_;
    const string name_;
    const GattSession session_;
    const GattDeviceService service_;
    const GattCharacteristic write_characteristic_;
    const GattCharacteristic notify_characteristic_;
    mutable GattCharacteristic::ValueChanged_revoker notification_revoker_;
    mutable BluetoothLEDevice::ConnectionStatusChanged_revoker connection_changed_revoker_;

public:
    static shared_ptr<DeviceSession> MakeSession(uint64_t bluetoothAddr)
    {
        auto device = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddr).get();
        GattSession session = GattSession::FromDeviceIdAsync(device.BluetoothDeviceId()).get();

        const auto services = device.GetGattServicesForUuidAsync(kServiceGuid).get().Services();
        if (services.Size() < 1)
        {
            Log("Failed to get services for %ull", bluetoothAddr);
            return nullptr;
        }
        GattDeviceService service = services.GetAt(0);

        const auto notifChs = service.GetCharacteristicsForUuidAsync(kNotifyGuid).get().Characteristics();
        if (notifChs.Size() < 1)
        {
            Log("Failed to get notify characteristic");
            return nullptr;
        }
        GattCharacteristic notifyCh = notifChs.GetAt(0);

        const auto wrChs = service.GetCharacteristicsForUuidAsync(kWriteGuid).get().Characteristics();
        if (wrChs.Size() < 1)
        {
            Log("Failed to get write characteristic");
            return nullptr;
        }
        GattCharacteristic writeCh = wrChs.GetAt(0);
        
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

    void NotifyCharacteristicValueChanged(const GattValueChangedEventArgs& args) const
    {
        if (gDataReceivedCallback != nullptr)
        {
            const IBuffer& data = args.CharacteristicValue();
            gDataReceivedCallback(identifier_.c_str(), data.Length(), data.data());
        }
    }

    void Connect() const
    {
        connection_changed_revoker_ = device_.ConnectionStatusChanged(auto_revoke, [this](auto&& dev, auto&& result)
        {
            internalConnectionChangedHandler(dev, identifier_);
        });

        session_.MaintainConnection(true);

        notification_revoker_ = notify_characteristic_.ValueChanged(auto_revoke, [this](auto&& ch, auto&& args)
        {
            NotifyCharacteristicValueChanged(args);
        });
        auto configResult = notify_characteristic_
            .WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify)
            .get();

        if (gDeviceConnectedCallback)
        {
            gDeviceConnectedCallback(identifier_.c_str());
        }
    }

    void Disconnect() const
    {
        notification_revoker_.revoke();
        connection_changed_revoker_.revoke();

        session_.MaintainConnection(false);
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

    auto result = gWatcher.Received(ReceivedDeviceFoundEvent);

    gWatcher.Stopped([=](auto&&, auto&&)
    {
        Log("Watcher Stopped\n");
        if (gListenerStoppedCallback)
        {
            gListenerStoppedCallback();
        }
    });

    if (gDeviceFoundCallback)
    {
        scoped_lock lk(gMapMutex);
        for (const auto& kv : gDevicesByIdentifier)
        {
            gDeviceFoundCallback(kv.first.c_str(), kv.second->DeviceName().c_str());
        }
    }

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
    shared_ptr<DeviceSession> session = nullptr;

    {
        scoped_lock lk(gMapMutex);
        session = gDevicesByIdentifier[identifier];
        if (session == nullptr) return;
    }
    session->Disconnect();
}

void godice_send(const char* id, uint32_t data_size, uint8_t* data)
{
    const string identifier(id);
    
    const DataWriter writer;
    writer.WriteBytes(winrt::array_view(data, data + data_size));

    const IBuffer buf = writer.DetachBuffer();

    std::thread([buf, identifier]()
    {
        internalSend(identifier, buf);
    }).detach();
}

static void internalSend(const string& identifier, const IBuffer& buffer)
{
    shared_ptr<DeviceSession> session;

    {
        scoped_lock lk(gMapMutex);
        session = gDevicesByIdentifier[identifier];
        if (session == nullptr) return;
    }

    auto status = session->GetWriteCharacteristic().WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).get();
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
    shared_ptr<DeviceSession> session;
    {
        scoped_lock lk(gMapMutex);
        session = gDevicesByIdentifier[identifier];
        if (session == nullptr) return;
    }
    session->Connect();
}

static void ReceivedDeviceFoundEvent(const BluetoothLEAdvertisementWatcher& watcher,
                          const BluetoothLEAdvertisementReceivedEventArgs& args)
{
    uint64_t btAddr = args.BluetoothAddress();
    string identifier = std::to_string(btAddr);

    std::thread([identifier, btAddr]
    {
        {
            std::scoped_lock lock(gMapMutex);
            if (gDevicesInProgress.contains(identifier)) return;
            if (gDevicesByIdentifier.count(identifier) == 0)
            {
                gDevicesInProgress.insert(identifier);
            } else
            {
                if (gDeviceFoundCallback)
                {
                    const auto& device = gDevicesByIdentifier[identifier];
                    gDeviceFoundCallback(identifier.c_str(), device->DeviceName().c_str());
                    return;
                }
            }
        }

        auto newSession = DeviceSession::MakeSession(btAddr);
        if (newSession == nullptr)
        {
            Log("Failed to create new session\n");
            return;
        }

        {
            std::scoped_lock lock(gMapMutex);
            gDevicesByIdentifier.emplace(std::make_pair(identifier, newSession));
            gDevicesInProgress.erase(identifier);

            if (gDeviceFoundCallback)
            {
                gDeviceFoundCallback(identifier.c_str(), newSession->DeviceName().c_str());
            }
        }
    }).detach();
}

void godice_stop_listening()
{
    gWatcher.Stop();
}
