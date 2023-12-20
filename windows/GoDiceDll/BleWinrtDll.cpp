// BleWinrtDll.cpp
//

#include "stdafx.h"
#include <winrt/windows.foundation.h>

#include "BleWinrtDll.h"

#include <ppltasks.h>
#include <future>

#pragma comment(lib, "windowsapp")

// macro for file, see also https://stackoverflow.com/a/14421702
#define __WFILE__ L"BleWinrtDll.cpp"

using namespace std;

using namespace winrt;

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;

using namespace Windows::Storage::Streams;

using std::shared_ptr;

static BluetoothLEAdvertisementWatcher gWatcher = nullptr;

class DeviceSession;

static GDDeviceFoundCallbackFunction gDeviceFoundCallback = nullptr;
static GDDataCallbackFunction gDataReceivedCallback = nullptr;
static GDDeviceConnectedCallbackFunction gDeviceConnectedCallback = nullptr;
static GDDeviceDisconnectedCallbackFunction gDeviceDisconnectedCallback = nullptr;
static GDListenerStoppedCallbackFunction gListenerStoppedCallback = nullptr;
static GDLogger gLogger = nullptr;

static std::mutex gMapMutex;
static std::map<string, shared_ptr<DeviceSession>> gDevicesByIdentifier;

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
static void internalConnect(const char* identifier);
static void internalSend(const char* id, uint32_t data_size, uint8_t* data);

struct DeviceSession
{
private:
    const shared_ptr<BluetoothLEDevice> device_;
    const string identifier;
    GattCharacteristic write_characteristic_ = nullptr;
    GattCharacteristic notify_characteristic_ = nullptr;

public:
    DeviceSession(const shared_ptr<BluetoothLEDevice>& dev, const string& ident) : device_(dev), identifier(ident)
    {
    }

    void SetNotifyCharacteristic(const GattCharacteristic& nCh)
    {
        notify_characteristic_ = nCh;

        notify_characteristic_.ValueChanged([=](auto&& ch, auto&& args)
        {
            if (gDataReceivedCallback != nullptr)
            {
                const IBuffer& data = args.CharacteristicValue();
                gDataReceivedCallback(identifier.c_str(), data.Length(), data.data());
            }
        });
    }

    void SetWriteCharacteristic(const GattCharacteristic& wCh)
    {
        write_characteristic_ = wCh;
    }

    const shared_ptr<BluetoothLEDevice> GetDevice() { return device_; }

    auto GetWriteCharacteristic() const -> const GattCharacteristic&
    {
        return write_characteristic_;
    }

    ~DeviceSession()
    {
        if (notify_characteristic_)
        {
            notify_characteristic_.ValueChanged([](auto&& ch, auto&& args)
            {
            });
        }
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
        cout << "Stopped";
        godice_stop_listening();
    });

    gWatcher.Start();
}

void godice_connect(const char* identifier)
{
    internalConnect(identifier);
}

void godice_disconnect(const char* identifier)
{
    auto session = gDevicesByIdentifier[identifier];
    gDevicesByIdentifier.erase(identifier);
}

void godice_send(const char* identifier, uint32_t data_size, uint8_t* data)
{
    internalSend(identifier, data_size, data);
}

static void internalSend(const char* id, uint32_t data_size, uint8_t* data)
{
    auto session = gDevicesByIdentifier[id];

    Log("Connection status is {}\n", (int)session->GetDevice()->ConnectionStatus());

    DataWriter writer;
    writer.WriteBytes(winrt::array_view(data, data + data_size));

    auto buf = writer.DetachBuffer();

   // Log("Preparing to send data: {} bytes, {} first value\n", (int)buf.Length(), (int)buf.data()[0]);

    auto result = session->GetWriteCharacteristic().WriteValueWithResultAsync(buf, GattWriteOption::WriteWithResponse).get();

    auto status = result.Status();
    auto err = result.ProtocolError();
   // Log("Status is {}\n", (int)result);
}

static void internalConnect(const char* id)
{
    Log("Preparing to connect\n");
    string identifier(id);
    auto session = gDevicesByIdentifier[identifier];
    auto& device = session->GetDevice();

    device->ConnectionStatusChanged([=](const BluetoothLEDevice& dev, auto&& args)
    {
        if (dev.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
        {
            if (gDeviceDisconnectedCallback)
            {
                gDeviceDisconnectedCallback(identifier.c_str());
            }
        }
    });

    Log("get services\n");
    device->GetGattServicesForUuidAsync(kServiceGuid).Completed([=](auto&& getServicesAsync, auto&& result)
    {
        const auto& services = getServicesAsync.get().Services();

        for (const GattDeviceService& svc : services)
        {
            if (svc.Uuid() != kServiceGuid) continue;

            Log("Starting GetCharacteristics\n");

            svc.GetCharacteristicsAsync().Completed([=](auto&& getChsAsync, auto&& chResult)
            {
                for (const auto chs = getChsAsync.get().Characteristics(); const GattCharacteristic& ch : chs)
                {
                    if (IsEqualGUID(ch.Uuid(), kWriteGuid))
                    {
                        session->SetWriteCharacteristic(ch);
                    }
                    else if (IsEqualGUID(ch.Uuid(), kNotifyGuid))
                    {
                        ch.WriteClientCharacteristicConfigurationDescriptorAsync(
                            GattClientCharacteristicConfigurationDescriptorValue::Notify).Completed(
                            [=](auto&& writeConfigStatusAsync, auto&& wcsResult)
                            {
                                auto writeConfigStatus = writeConfigStatusAsync.get();

                                if (writeConfigStatus == GattCommunicationStatus::Success)
                                {
                                    session->SetNotifyCharacteristic(ch);

                                    if (gDeviceConnectedCallback)
                                    {
                                        gDeviceConnectedCallback(identifier.c_str());
                                    }
                                }
                                else
                                {
                                    throw exception("oops");
                                }
                            });
                    }
                }
            });
        }
    });
}

static void ReceivedEvent(const BluetoothLEAdvertisementWatcher& watcher,
                          const BluetoothLEAdvertisementReceivedEventArgs& args)
{
    const BluetoothLEAdvertisement& ad = args.Advertisement();


    uint64_t btAddr = args.BluetoothAddress();
    string identifier = to_string(btAddr);
    {
        std::scoped_lock<std::mutex> lock(gMapMutex);
        if (gDevicesByIdentifier.count(identifier) > 0)
        {
            return;
        }
    }

    BluetoothLEDevice::FromBluetoothAddressAsync(btAddr).Completed([=](auto&& deviceAsync, auto&& res)
    {
        shared_ptr<BluetoothLEDevice> device = std::make_shared<BluetoothLEDevice>(deviceAsync.get());

        {
            std::scoped_lock<std::mutex> lock(gMapMutex);
            if (gDevicesByIdentifier.count(identifier) > 0)
            {
                return;
            }


            shared_ptr<DeviceSession> session = std::make_shared<DeviceSession>(device, identifier);
            gDevicesByIdentifier[identifier] = session;

            if (gDeviceFoundCallback)
            {
                gDeviceFoundCallback(identifier.c_str(), to_string(device->Name()).c_str());
            }
        }
    });
}

void godice_stop_listening()
{
    const std::scoped_lock<std::mutex> lock(gMapMutex);
    gWatcher.Stop();
    gDevicesByIdentifier.clear();
}
