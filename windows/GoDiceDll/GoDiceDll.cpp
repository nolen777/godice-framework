// GoDiceDll.cpp
//

#include "GoDiceDll.h"

#include "stdafx.h"

#include <ppltasks.h>
#include <future>
#include <semaphore>
#include <unordered_set>

#include <pplawait.h>

#include "WorkQueue.h"

#pragma comment(lib, "windowsapp")

// macro for file, see also https://stackoverflow.com/a/14421702
#define __WFILE__ L"BleWinrtDll.cpp"

using namespace winrt;

using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;

using namespace Windows::Storage::Streams;

using Windows::Foundation::IAsyncOperation;
using Windows::Foundation::IInspectable;

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
using std::counting_semaphore;
using std::exception;
using std::function;
using std::shared_future;
using std::mutex;
using std::queue;
using std::scoped_lock;
using std::shared_ptr;
using std::string;
using std::unique_lock;
using std::unordered_map;
using std::pmr::unordered_set;

static void Log(const char* str);
static WorkQueue gBluetoothQueue;
static WorkQueue gCallbackQueue;

static binary_semaphore gMapSema = binary_semaphore(1);
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

static IAsyncOperation<bool> internalReceivedDeviceFoundEvent(uint64_t btAddr);
static IAsyncOperation<bool> internalConnect(const string& identifier);
static IAsyncOperation<bool> internalSend(const string& identifier, const IBuffer& buffer);
static void internalConnectionChangedHandler(const BluetoothLEDevice& dev, const string& identifier);

class DeviceSession : std::enable_shared_from_this<DeviceSession>
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

    static inline binary_semaphore use_sema_ = binary_semaphore(1);
    bool connected_ = false;

    IAsyncOperation<bool> lockedDisconnect()
    {
            if (notify_characteristic_ != nullptr)
            {
                try
                {
                    co_await notify_characteristic_.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::None);
                    notify_characteristic_.ValueChanged(std::exchange(notify_token_, {}));
                }
                catch (winrt::hresult_error& e)
                {
                    NamedLog("Failed to disconnect notify characteristic\n");
                }
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

            co_return true;
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

    IAsyncOperation<bool> lockedConnect()
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
            const auto accessStatus = co_await service_.RequestAccessAsync();
            if (accessStatus != DeviceAccessStatus::Allowed)
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

            NamedLog("Connection status is {}\n", connected_);
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

    IAsyncOperation<bool> lockedSend(const IBuffer& msg)
    {
        if (!connected_)
        {
            NamedLog("Attempting to write while not connected");
            co_return false;
        }
        
        if (write_characteristic_ == nullptr)
        {
            NamedLog("No write characteristic found for\n");
            co_return false;
        }

        try
        {
            auto status = co_await write_characteristic_.WriteValueAsync(msg, GattWriteOption::WriteWithoutResponse);
            if (status != GattCommunicationStatus::Success)
            {
                NamedLog("Write data failed with status {}\n", (int)status);
                co_return false;
            }

            co_return true;
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

        co_return false;
    }
    
public:
    static Concurrency::task<shared_ptr<DeviceSession>> MakeSession(uint64_t bluetoothAddr)
    {
        return Concurrency::create_task([bluetoothAddr]
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

            return shared_ptr<DeviceSession>(nullptr);
        });
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
            const string ident = this->identifier_;
            gCallbackQueue.Enqueue([data, ident]
            {
                gDataReceivedCallback(ident.c_str(), data.Length(), data.data());
            });
        }
    }

    IAsyncOperation<bool> Connect()
    {
        bool success = false;
        
        use_sema_.acquire();
        if (device_.ConnectionStatus() == BluetoothConnectionStatus::Disconnected && notify_characteristic_ != nullptr)
        {
            lockedDisconnect();
        }
        
        if (connected_)
        {
            NamedLog("Already connected\n");
            success = true;
        }
        else
        {
            bool result = co_await lockedConnect();
            if (result)
            {
                success = true;
            }
            else
            {
                lockedDisconnect();
            }
        }
        use_sema_.release();

        co_return success;
    }

    IAsyncOperation<bool> Send(const IBuffer& msg)
    {
        use_sema_.acquire();
        NamedLog("Attempting to write {} bytes\n", msg.Length());
        auto result = co_await lockedSend(msg);

        use_sema_.release();

        co_return result;
    }

    IAsyncOperation<bool> Disconnect()
    {
        try
        {
            use_sema_.acquire();
            auto result = co_await lockedDisconnect();
            use_sema_.release();

            const string ident = identifier_;
            gCallbackQueue.Enqueue([ident]
            {
                if (gDeviceDisconnectedCallback) {
                    gDeviceDisconnectedCallback(ident.c_str());
                }
            });

            co_return result;
        }
        catch (winrt::hresult_error& e)
        {
            NamedLog("Caught exception while disconnecting {}\n", e.code().value);
            co_return false;
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
    gBluetoothQueue.Enqueue([=]
    {
        gDeviceFoundCallback = deviceFoundCallback;
        gDataReceivedCallback = dataReceivedCallback;
        gDeviceConnectedCallback = deviceConnectedCallback;
        gDeviceConnectionFailedCallback = deviceConnectionFailedCallback;
        gDeviceDisconnectedCallback = deviceDisconnectedCallback;
        gListenerStoppedCallback = listenerStoppedCallback;
    });
}

void godice_set_logger(GDLogger logger)
{
    gBluetoothQueue.Enqueue([logger]
    {
        gLogger = logger;
    });
}

void godice_start_listening()
{
    gBluetoothQueue.Enqueue([]
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

            gCallbackQueue.Enqueue([]
            {
                if (gListenerStoppedCallback)
                {
                    gListenerStoppedCallback();
                }
            });
        });

        if (gDeviceFoundCallback)
        {
            // Make a copy of sessions inside the lock
            gMapSema.acquire();
            unordered_map<string, shared_ptr<DeviceSession>> sessions = gDevicesByIdentifier;
            gMapSema.release();

            gCallbackQueue.Enqueue([sessions]
            {
                for (const auto& kv : sessions)
                {
                    gDeviceFoundCallback(kv.first.c_str(), kv.second->DeviceName().c_str());
                }
            });
        }

        gWatcher.Start();
    });
}

void godice_connect(const char* inIdent)
{
    string identifier = inIdent;
    gBluetoothQueue.Enqueue([identifier]
    {
        Log("Trying to connect to {}\n", identifier);
        bool result = internalConnect(identifier).get();
        Log("Result was {}\n", result);
    });
}

void godice_disconnect(const char* inIdent)
{
    string identifier = inIdent;
    gBluetoothQueue.Enqueue([identifier]
    {
        shared_ptr<DeviceSession> session = nullptr;

        gMapSema.acquire();
        session = gDevicesByIdentifier[identifier];
        gMapSema.release();
    
        if (session == nullptr) return;
        
        bool result = session->Disconnect().get();
    });
}

void godice_send(const char* id, uint32_t data_size, uint8_t* data)
{
    const string identifier(id);

    const DataWriter writer;
    writer.WriteBytes(winrt::array_view(data, data + data_size));

    const IBuffer buf = writer.DetachBuffer();
    
    gBluetoothQueue.Enqueue([identifier, buf]
    {
        bool success = internalSend(identifier, buf).get();
    });
}

static IAsyncOperation<bool> internalSend(const string& identifier, const IBuffer& buffer)
{
    gMapSema.acquire();
    const shared_ptr<DeviceSession> session = gDevicesByIdentifier[identifier];
    gMapSema.release();

    if (session == nullptr)
    {
        Log("No session found for {}\n", identifier);
    }
    else
    {
        co_return co_await session->Send(buffer);
    }
    co_return false;
}

static void internalConnectionChangedHandler(const BluetoothLEDevice& dev, const string& identifier)
{
    gBluetoothQueue.Enqueue([dev, identifier]
    {
        if (dev.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
        {
            gMapSema.acquire();
            const auto& session = gDevicesByIdentifier[identifier];
            gMapSema.release();
        
            if (session != nullptr)
            {
                session->Disconnect().get();
            }
        }
    });
}

static IAsyncOperation<bool> internalConnect(const string& inIdent)
{
    string identifier = inIdent;
    shared_ptr<DeviceSession> session;
    bool success = false;
    
    gMapSema.acquire();
    session = gDevicesByIdentifier[identifier];
    gMapSema.release();
    
    if (session == nullptr)
    {
        Log("No session for {}\n", identifier);
        success = false;
    }
    else
    {
        success = co_await session->Connect();
    }
    
    if (success && gDeviceConnectedCallback)
    {
        gCallbackQueue.Enqueue([identifier]
        {
            gDeviceConnectedCallback(identifier.c_str());
        });
    }
    if (!success && gDeviceConnectionFailedCallback)
    {
        gCallbackQueue.Enqueue([identifier]
        {
            gDeviceConnectionFailedCallback(identifier.c_str());
        });
    }
    
    co_return success;
}

static void ReceivedDeviceFoundEvent(const BluetoothLEAdvertisementWatcher& watcher,
                                     const BluetoothLEAdvertisementReceivedEventArgs& args)
{
    uint64_t btAddr = args.BluetoothAddress();
    string identifier = std::to_string(btAddr);
    internalReceivedDeviceFoundEvent(btAddr);
}

static IAsyncOperation<bool> internalReceivedDeviceFoundEvent(const uint64_t inBtAddr) {
    auto btAddr = inBtAddr;
    //
    auto identifier =  std::to_string(btAddr);;
    
    shared_ptr<DeviceSession> session = nullptr;

    bool needsNewSession = true;
    gMapSema.acquire();
    if (gDevicesInProgress.contains(identifier))
    {
        needsNewSession = false;
        gDevicesInProgress.insert(identifier);
    }
    else if (gDevicesByIdentifier.contains(identifier))
    {
        needsNewSession = false;
    }
    gMapSema.release();

    if (needsNewSession)
    {
        session = co_await DeviceSession::MakeSession(btAddr);
        
        if (session == nullptr)
        {
            Log("Failed to create new session\n");
        }
        else
        {
            gMapSema.acquire();
            gDevicesByIdentifier.emplace(std::make_pair(identifier, session));
            gDevicesInProgress.erase(identifier);
            gMapSema.release();
        }
    }
    else
    {
        gMapSema.acquire();
        session = gDevicesByIdentifier[identifier];
        gMapSema.release();
    }

    if (session && gDeviceFoundCallback)
    {
        gCallbackQueue.Enqueue([identifier, session]
        {
            gDeviceFoundCallback(identifier.c_str(), session->DeviceName().c_str());
        });
    }
    co_return session != nullptr;
}

void godice_stop_listening()
{
    gBluetoothQueue.Enqueue([]
    {
        gWatcher.Stop();
    });
}

void godice_reset()
{
    gBluetoothQueue.Enqueue([]
    {
        gWatcher.Stop();
    
        gMapSema.acquire();

        while (gDevicesInProgress.size() > 0)
        {
            gMapSema.release();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            gMapSema.acquire();
        }
    
        gDevicesByIdentifier.clear();
    
        gMapSema.release();
    });
}

