// GoDiceDll.cpp
//

#include "GoDiceDll.h"

#include "stdafx.h"

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
static GDDeviceConnectionFailedCallbackFunction gDeviceConnectionFailedCallback = nullptr;
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
    BluetoothLEDevice device_;
    const uint64_t bluetoothAddress_;
    const string identifier_;
    const string name_;
    GattDeviceService service_;
    GattCharacteristic write_characteristic_;
    GattCharacteristic notify_characteristic_;
    event_token notify_token_;
    event_token connection_status_changed_token_;

    mutex connection_lock_;

    void lockedDisconnect()
    {
        scoped_lock lk(connection_lock_);
        if (notify_characteristic_ != nullptr)
        {
            notify_characteristic_.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::None);
            notify_characteristic_.ValueChanged(std::exchange(notify_token_, {}));
            notify_characteristic_ = nullptr;
        }
        write_characteristic_ = nullptr;
        
        if (service_ != nullptr)
        {
            service_.Close();
            service_ = nullptr;
        }
        if (device_ != nullptr)
        {
            device_.ConnectionStatusChanged(std::exchange(connection_status_changed_token_, {}));
        
            device_.Close();
            device_ = nullptr;
        }
    }
    
public:
    static shared_ptr<DeviceSession> MakeSession(uint64_t bluetoothAddr)
    {
        try
        {
            auto device = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddr).get();

            return std::make_shared<DeviceSession>(device, bluetoothAddr, nullptr, nullptr,
                                                   nullptr);
        }
        catch (std::exception& e)
        {
            Log("Caught exception while creating session {}\n", e.what());
        }
        catch (winrt::hresult_error& e)
        {
            Log("Caught exception while creating session {}\n", to_string(e.message()));
        }
        catch (...)
        {
            auto e = std::current_exception();
            Log("Caught exception while creating new session\n");
        }

        return nullptr;
    }

    DeviceSession(
        const BluetoothLEDevice& dev,
        const uint64_t btAddr,
        const GattDeviceService& service,
        const GattCharacteristic& notifyCh,
        const GattCharacteristic& writeCh
    ) : device_(dev), bluetoothAddress_(btAddr), identifier_(std::to_string(btAddr)), name_(to_string(dev.Name())), service_(service),
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

    void Connect()
    {
        try
        {
            lockedDisconnect();

            scoped_lock lk(connection_lock_);
            device_ = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress_).get();

            connection_status_changed_token_ = device_.ConnectionStatusChanged([this](auto&& dev, auto&& args)
            {
                internalConnectionChangedHandler(dev, identifier_);
            });
            
            const auto servicesResult = device_.GetGattServicesForUuidAsync(kServiceGuid).get();
            if (servicesResult.Status() != GattCommunicationStatus::Success)
            {
                Log("Failed to get services for {}\n", name_);
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }
            const auto services = servicesResult.Services();
            if (services.Size() < 1)
            {
                Log("Failed to get services for {}\n", name_);
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }
            service_ = services.GetAt(0);

            if (service_.RequestAccessAsync().get() != DeviceAccessStatus::Allowed)
            {
                Log("Failed to get access to service");
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }

            const auto notifChsResponse = service_.GetCharacteristicsForUuidAsync(kNotifyGuid).
                                           get();
            if (notifChsResponse.Status() != GattCommunicationStatus::Success)
            {
                Log("Failed to get notify characteristic for {}\n", name_);
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }
            auto notifChs = notifChsResponse.Characteristics();
            if (notifChs.Size() < 1)
            {
                Log("Failed to get notify characteristic for {}\n", name_);
                
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }
            notify_characteristic_ = notifChs.GetAt(0);

            auto configResult = notify_characteristic_
                                .WriteClientCharacteristicConfigurationDescriptorAsync(
                                    GattClientCharacteristicConfigurationDescriptorValue::Notify)
                                .get();
            if (configResult != GattCommunicationStatus::Success)
            {
                Log("Failed to get set notification config for {}\n", name_);
                
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }

            const auto wrChsResult = service_.GetCharacteristicsForUuidAsync(kWriteGuid).get();
            if (wrChsResult.Status() != GattCommunicationStatus::Success)
            {
                Log("Failed to get write characteristic for {}\n", name_);
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }
            const auto wrChs = wrChsResult.Characteristics();
            if (wrChs.Size() < 1)
            {
                Log("Failed to get write characteristic for {}\n", name_);
                gDeviceConnectionFailedCallback(identifier_.c_str());
                return;
            }
            write_characteristic_ = wrChs.GetAt(0);

            notify_token_ = notify_characteristic_.ValueChanged([this](auto&& ch, auto&& args)
            {
                NotifyCharacteristicValueChanged(args);
            });
        }
        catch (std::exception& e)
        {
            Log("Caught exception while connecting {}\n", e.what());
            gDeviceConnectionFailedCallback(identifier_.c_str());
        }
        catch (winrt::hresult_error& e)
        {
            Log("Caught exception while connecting {}\n", to_string(e.message()));
            gDeviceConnectionFailedCallback(identifier_.c_str());
        }
        catch (...)
        {
            auto e = std::current_exception();
            Log("Caught exception while connecting\n");
            gDeviceConnectionFailedCallback(identifier_.c_str());
        }

        if (gDeviceConnectedCallback)
        {
            gDeviceConnectedCallback(identifier_.c_str());
        }
    }

    fire_and_forget Send(const IBuffer& msg)
    {
        scoped_lock lk(connection_lock_);
        if (write_characteristic_ == nullptr)
        {
            Log("No write characteritic found for {}\n", identifier_);
            co_return;
        }

        try
        {
            auto status = co_await write_characteristic_.WriteValueAsync(msg, GattWriteOption::WriteWithoutResponse);
            if (status != GattCommunicationStatus::Success)
            {
                Log("Write data failed for {} with status {}\n", identifier_, (int)status);
            }
        }
        catch (std::exception& e)
        {
            Log("Caught exception while writing! {}\n", e.what());
        }
        catch (winrt::hresult_error& e)
        {
            Log("Caught exception while writing! {}\n", to_string(e.message()));
        }
        catch (...)
        {
            auto e = std::current_exception();
            Log("Caught exception while writing!\n");
        }
    }

    void Disconnect()
    {
        scoped_lock lk(connection_lock_);
        lockedDisconnect();
        
        if (fireCallback && gDeviceDisconnectedCallback) {
            gDeviceDisconnectedCallback(identifier_.c_str());
        }
    }

    const string& DeviceName() const { return name_; }

    ~DeviceSession()
    {
        Disconnect();
    }
};

void godice_set_callbacks(
    GDDeviceFoundCallbackFunction deviceFoundCallback,
    GDDataCallbackFunction dataReceivedCallback,
    GDDeviceConnectedCallbackFunction deviceConnectedCallback,
    GDDeviceConnectionFailedCallbackFunction deviceConnectionFailedCallback,
    GDDeviceDisconnectedCallbackFunction deviceDisconnectedCallback,
    GDListenerStoppedCallbackFunction listenerStoppedCallback)
{
    gDeviceFoundCallback = deviceFoundCallback;
    gDataReceivedCallback = dataReceivedCallback;
    gDeviceConnectedCallback = deviceConnectedCallback;
    gDeviceConnectionFailedCallback = deviceConnectionFailedCallback;
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
    GattCharacteristic writeCharacteristic = nullptr;

    {
        scoped_lock lk(gMapMutex);
        session = gDevicesByIdentifier[identifier];
        if (session == nullptr)
        {
            Log("No session found for {}\n", identifier);
            return;
        }
    }

    writeCharacteristic = session->GetWriteCharacteristic();
    if (writeCharacteristic == nullptr)
    {
        Log("No write characteritic found for {}\n", identifier);
        return;
    }

    try
    {
        session->GetWriteCharacteristic().WriteValueAsync(buffer, GattWriteOption::WriteWithoutResponse).Completed(
            [](auto&& ch, auto&& status)
            {
                Log("Wrote data with status of {}\n", (int)status);
            });
    }
    catch (std::exception& e)
    {
        Log("Caught exception while writing! {}\n", e.what());
    }
    catch (winrt::hresult_error& e)
    {
        Log("Caught exception while writing! {}\n", to_string(e.message()));
    }
    catch (...)
    {
        auto e = std::current_exception();
        Log("Caught exception while writing!\n");
    }
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

    std::scoped_lock lock(gMapMutex);
    if (gDevicesInProgress.contains(identifier)) return;
    if (gDevicesByIdentifier.count(identifier) == 0)
    {
        gDevicesInProgress.insert(identifier);
        std::thread([identifier, btAddr]
        {
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
    else
    {
        if (gDeviceFoundCallback)
        {
            const auto& session = gDevicesByIdentifier[identifier];
            gDeviceFoundCallback(identifier.c_str(), session->DeviceName().c_str());
        }
        return;
    }
}

void godice_stop_listening()
{
    gWatcher.Stop();

    gMapMutex.lock();

    while (gDevicesInProgress.count() > 0)
    {
        gMapMutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        gMapMutex.lock();
    }

    for (const auto& kv : gDevicesByIdentifier)
    {
        kv.second->Disconnect();
    }
    gDevicesByIdentifier.clear();
    
    gMapMutex.unlock();
}
