// GoDiceDll.cpp
//

#include "GoDiceDll.h"

#include "stdafx.h"

#include <ppltasks.h>
#include <future>
#include <semaphore>
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

using std::binary_semaphore;
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

    binary_semaphore use_sema_;
    bool connected_ = false;

    void lockedDisconnect()
    {
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
        }

        connected_ = false;
    }

    static string GDSRErrorString(const GattDeviceServicesResult& result)
    {
        switch (result.Status())
        {
        case GattCommunicationStatus::Success:
            return "success";
        case GattCommunicationStatus::Unreachable:
            return "unreachable";
        case GattCommunicationStatus::ProtocolError:
            return "protocol error: " + std::to_string(result.ProtocolError().Value());
        case GattCommunicationStatus::AccessDenied:
            return "access denied";
        default:
            return "unknown status";
        }
    }

    static string GCRErrorString(const GattCharacteristicsResult& result)
    {
        switch (result.Status())
        {
        case GattCommunicationStatus::Success:
            return "success";
        case GattCommunicationStatus::Unreachable:
            return "unreachable";
        case GattCommunicationStatus::ProtocolError:
            return "protocol error: " + std::to_string(result.ProtocolError().Value());
        case GattCommunicationStatus::AccessDenied:
            return "access denied";
        default:
            return "unknown status";
        }
    }

    void NamedLog(const char* str)
    {
        Log(("[" + name_ + "] " + str).c_str());
    }
    
    template <class Args>
    void NamedLog(const char* str, Args&& args...)
    {
        auto prefixed = "[" + name_ + "] " + str;
        auto formattedStr = std::vformat(prefixed, std::make_format_args(args));
        Log(formattedStr.c_str());
    }

    Windows::Foundation::IAsyncOperation<bool> lockedConnect()
    {
        try
        {
            connected_ = false;

            NamedLog("Getting services\n");
            const auto servicesResult = co_await device_.GetGattServicesForUuidAsync(kServiceGuid, BluetoothCacheMode::Cached);
            if (servicesResult.Status() != GattCommunicationStatus::Success)
            {
                auto errString = GDSRErrorString(servicesResult);
                NamedLog("Failed to get services, error `{}`\n", errString);
                
                co_return false;
            }
            const auto services = servicesResult.Services();
            if (services.Size() < 1)
            {
                NamedLog("Failed to get services\n");
                
                co_return false;
            }
            service_ = services.GetAt(0);

            NamedLog("Requesting access\n");
            if (service_.RequestAccessAsync().get() != DeviceAccessStatus::Allowed)
            {
                NamedLog("Failed to get access to service for {}\n", name_);
                
                co_return false;
            }
            
            NamedLog("Setting status changed handler\n");
            connection_status_changed_token_ = device_.ConnectionStatusChanged([this](auto&& dev, auto&& args)
            {
                internalConnectionChangedHandler(dev, identifier_);
            });

            NamedLog("Getting notify characteristic\n");
            const auto notifChsResponse = co_await service_.GetCharacteristicsForUuidAsync(kNotifyGuid, BluetoothCacheMode::Cached);
            if (notifChsResponse.Status() != GattCommunicationStatus::Success)
            {
                auto errString = GCRErrorString(notifChsResponse);
                NamedLog("Got a failure response from GetCharacteristicsForUuidAsync for notify characteristic with err `{}`\n", errString);
                
                co_return false;
            }
            auto notifChs = notifChsResponse.Characteristics();
            if (notifChs.Size() < 1)
            {
                NamedLog("Did not find any notification characteristics\n");
                
                co_return false;
            }
            notify_characteristic_ = notifChs.GetAt(0);

            if ((notify_characteristic_.CharacteristicProperties() & GattCharacteristicProperties::Notify) != GattCharacteristicProperties::Notify)
            {
                NamedLog("Did not find characteristic with expected Notify property\n");
                
                co_return false;
            }

            NamedLog("Writing configuration\n");
            auto configResult = co_await notify_characteristic_
                                .WriteClientCharacteristicConfigurationDescriptorAsync(
                                    GattClientCharacteristicConfigurationDescriptorValue::Notify);
            if (configResult != GattCommunicationStatus::Success)
            {
                NamedLog("Failed to get set notification config with result {}\n", int(configResult));
    
                co_return false;
            }

            NamedLog("Getting write characteristic\n");
            const auto wrChsResult = co_await service_.GetCharacteristicsForUuidAsync(kWriteGuid, BluetoothCacheMode::Cached);
            if (wrChsResult.Status() != GattCommunicationStatus::Success)
            {
                auto errString = GCRErrorString(wrChsResult);
                NamedLog("Got a failure response from GetCharacteristicsForUuidAsync for write characteristic with err `{}`\n", errString);

                co_return false;
            }
            const auto wrChs = wrChsResult.Characteristics();
            if (wrChs.Size() < 1)
            {
                NamedLog("Did not find any write characteristics\n");

                co_return false;
            }
            write_characteristic_ = wrChs.GetAt(0);

            if ((write_characteristic_.CharacteristicProperties() & GattCharacteristicProperties::Write) != GattCharacteristicProperties::Write)
            {
                NamedLog("Did not find characteristic with expected Write property\n");

                co_return false;
            }

            NamedLog("Setting value changed handler\n");
            notify_token_ = notify_characteristic_.ValueChanged([this](auto&& ch, auto&& args)
            {
                NotifyCharacteristicValueChanged(args);
            });

            connected_ = device_.ConnectionStatus() == BluetoothConnectionStatus::Connected;
            co_return connected_;
        }
        catch (std::exception& e)
        {
            NamedLog("Caught exception while connecting {}\n", e.what());
                
            co_return false;
        }
        catch (winrt::hresult_error& e)
        {
            const auto code = e.code();
            NamedLog("Caught exception code {} while connecting\n", e.code().value);
                
            co_return false;
        }
        catch (...)
        {
            auto e = std::current_exception();
            NamedLog("Caught exception while connecting\n");
                
            co_return false;
        }
    }

    void lockedSend(const IBuffer& msg)
    {
        if (!connected_)
        {
            NamedLog("Attempting to write while not connected");
            return;
        }
        
        if (write_characteristic_ == nullptr)
        {
            NamedLog("No write characteristic found for\n");
            return;
        }

        try
        {
            auto status = write_characteristic_.WriteValueAsync(msg, GattWriteOption::WriteWithoutResponse).get();
            if (status != GattCommunicationStatus::Success)
            {
                NamedLog("Write data failed with status {}\n", (int)status);
            }
        }
        catch (std::exception& e)
        {
            NamedLog("Caught exception while writing! {}\n", e.what());
        }
        catch (winrt::hresult_error& e)
        {
            NamedLog("Caught exception while writing! {}\n", to_string(e.message()));
        }
        catch (...)
        {
            auto e = std::current_exception();
            NamedLog("Caught exception while writing!\n");
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
        notify_characteristic_(notifyCh), write_characteristic_(writeCh), use_sema_(1)
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

    fire_and_forget Connect()
    {
        bool fireConnected = false;
        
        use_sema_.acquire();
        if (device_.ConnectionStatus() == BluetoothConnectionStatus::Disconnected && notify_characteristic_ != nullptr)
        {
            lockedDisconnect();
        }
        
        if (connected_)
        {
            NamedLog("Already connected\n");
            fireConnected = true;
        }
        else
        {
            bool result = co_await lockedConnect();
            if (result)
            {
                fireConnected = true;
            }
            else
            {
                lockedDisconnect();
            }
        }
        use_sema_.release();

        if (fireConnected)
        {
            if (gDeviceConnectedCallback)
            {
                gDeviceConnectedCallback(identifier_.c_str());
            }
        }
        else
        {
            if (gDeviceConnectionFailedCallback)
            {
                gDeviceConnectionFailedCallback(identifier_.c_str());
            }
        }
    }

    void Send(const IBuffer& msg)
    {
        use_sema_.acquire();
        NamedLog("Attempting to write {} bytes\n", msg.Length());
        lockedSend(msg);

        use_sema_.release();
    }

    void Disconnect()
    {
        use_sema_.acquire();
        lockedDisconnect();
        use_sema_.release();
        
        if (gDeviceDisconnectedCallback) {
            gDeviceDisconnectedCallback(identifier_.c_str());
        }
    }

    const string& DeviceName() const { return name_; }

    ~DeviceSession()
    {
        use_sema_.acquire();
        lockedDisconnect();

        device_.Close();
        device_ = nullptr;

        use_sema_.release();
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
    Log("Trying to connect to {}\n", identifier);
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

    std::thread([session]
    {
        session->Disconnect();
    }).detach();
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

    session->Send(buffer);
}

static void internalConnectionChangedHandler(const BluetoothLEDevice& dev, const string& identifier)
{
    if (dev.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
    {
        scoped_lock lk(gMapMutex);
        const auto& session = gDevicesByIdentifier[identifier];
        if (session != nullptr)
        {
            session->Disconnect();
        }
    }
}

static void internalConnect(const string& identifier)
{
    shared_ptr<DeviceSession> session;
    {
        scoped_lock lk(gMapMutex);
        session = gDevicesByIdentifier[identifier];
        if (session == nullptr)
        {
            Log("No session for {}\n", identifier);
            return;
        }
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
}

void godice_reset() {
    gWatcher.Stop();
    
    gMapMutex.lock();

    while (gDevicesInProgress.size() > 0)
    {
        gMapMutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        gMapMutex.lock();
    }
    
    gDevicesByIdentifier.clear();
    
    gMapMutex.unlock();
}
