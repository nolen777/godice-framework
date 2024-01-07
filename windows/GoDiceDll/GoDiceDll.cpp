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

static BluetoothLEAdvertisementWatcher g_watcher = nullptr;

class DeviceSession;

static GDDeviceFoundCallbackFunction g_device_found_callback = nullptr;
static GDDataCallbackFunction g_data_received_callback = nullptr;
static GDDeviceConnectedCallbackFunction g_device_connected_callback = nullptr;
static GDDeviceConnectionFailedCallbackFunction g_device_connection_failed_callback = nullptr;
static GDDeviceDisconnectedCallbackFunction g_device_disconnected_callback = nullptr;
static GDListenerStoppedCallbackFunction g_listener_stopped_callback = nullptr;
static GDLogger g_logger = nullptr;

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

static auto log(const char* str) -> void;
static WorkQueue g_bluetooth_queue("BluetoothQueue");
static WorkQueue g_callback_queue("CallbackQueue");

static unordered_map<string, shared_ptr<DeviceSession>> g_devices_by_identifier;
static unordered_set<string> g_devices_in_progress;

static inline constexpr guid k_service_guid = guid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid k_write_guid = guid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid k_notify_guid = guid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

static void log(const char* str)
{
    if (g_logger)
    {
        g_logger(str);
    }
}

template <typename ...P>
static void log(string &&format, P&&... args)
{
    if (g_logger)
    {
        auto formattedStr = std::vformat(format, std::make_format_args(args...));
        g_logger(formattedStr.c_str());
    }
}

static auto received_device_found_event(const BluetoothLEAdvertisementWatcher& watcher,
                                        const BluetoothLEAdvertisementReceivedEventArgs& args) -> void;

static auto on_queue_received_device_found_event(uint64_t btAddr) -> IAsyncOperation<bool>;
static auto on_queue_internal_connect(const string& identifier) -> IAsyncOperation<bool>;
static auto on_queue_send(const string& identifier, const IBuffer& buffer) -> IAsyncOperation<bool>;
static auto internal_connection_changed_handler(const BluetoothLEDevice& dev, const string& identifier) -> void;

class DeviceSession : std::enable_shared_from_this<DeviceSession>
{
private:
    BluetoothLEDevice device_;
    const uint64_t bluetoothAddress_;
    const string identifier_;
    const string name_;
    GattDeviceService service_;
    GattSession gatt_session_;
    GattCharacteristic write_characteristic_;
    GattCharacteristic notify_characteristic_;
    event_token notify_token_;
    event_token connection_status_changed_token_;

    static inline binary_semaphore use_sema_ = binary_semaphore(1);
    bool connected_ = false;

    IAsyncOperation<bool> lockedDisconnect()
    {
        NamedLog("Attempting to disconnect\n");
        connected_ = false;
        
        if (notify_characteristic_ != nullptr)
        {
            try
            {
                co_await notify_characteristic_.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::None);
                notify_characteristic_.ValueChanged(std::exchange(notify_token_, {}));
            }
            catch (winrt::hresult_error& e)
            {
                NamedLog("Failed to disconnect notify characteristic {}\n", e.code().value);
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
        log(("[" + name_ + "] " + str).c_str());
    }
    
    template <typename ...P>
    void NamedLog(string &&format, P&&... args)
    {
        auto prefixed = "[" + name_ + "] " + format;
        auto formattedStr = std::vformat(prefixed, std::make_format_args(args...));
        log(formattedStr.c_str());
    }

    IAsyncOperation<bool> lockedConnect()
    {
        try
        {
            connected_ = false;

            NamedLog("Getting session");
            gatt_session_ = co_await GattSession::FromDeviceIdAsync(device_.BluetoothDeviceId());
            if (gatt_session_ == nullptr)
            {
                NamedLog("Failed to get session\n");
                
                co_return false;
            }
            else
            {
                gatt_session_.SessionStatusChanged([this](const GattSession& session, const GattSessionStatusChangedEventArgs& args)
                {
                    NamedLog("Session status changed to {}, error was {}\n", int(args.Status()), int(args.Error()));
                });
                gatt_session_.MaintainConnection(true);
            }
            
            NamedLog("Getting services\n");
            const auto servicesResult = co_await device_.GetGattServicesForUuidAsync(k_service_guid, BluetoothCacheMode::Cached);
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
                internal_connection_changed_handler(dev, identifier_);
            });

            NamedLog("Getting notify characteristic\n");
            const auto notifChsResponse = co_await service_.GetCharacteristicsForUuidAsync(k_notify_guid, BluetoothCacheMode::Cached);
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
            
            NamedLog("Setting value changed handler\n");
            notify_token_ = notify_characteristic_.ValueChanged([this](auto&& ch, auto&& args)
            {
                notify_characteristic_value_changed(args);
            });
            
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
            const auto wrChsResult = co_await service_.GetCharacteristicsForUuidAsync(k_write_guid, BluetoothCacheMode::Cached);
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
                log("Caught exception while creating session {}\n", e.what());
            }
            catch (winrt::hresult_error& e)
            {
                log("Caught exception while creating session {}\n", to_string(e.message()));
            }
            catch (...)
            {
                auto e = std::current_exception();
                log("Caught exception while creating new session\n");
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
        notify_characteristic_(notifyCh), write_characteristic_(writeCh), gatt_session_(nullptr)
    {
    }

    void notify_characteristic_value_changed(const GattValueChangedEventArgs& args) const
    {
        if (g_data_received_callback != nullptr)
        {
            const IBuffer& data = args.CharacteristicValue();
            const string ident = this->identifier_;
            g_callback_queue.enqueue([data, ident]
            {
                g_data_received_callback(ident.c_str(), data.Length(), data.data());
            });
        }
    }

    IAsyncOperation<bool> Connect()
    {
        bool success = false;
        
        use_sema_.acquire();
        if (device_.ConnectionStatus() == BluetoothConnectionStatus::Disconnected && notify_characteristic_ != nullptr)
        {
            co_await lockedDisconnect();
        }
        
        if (connected_)
        {
            NamedLog("Already connected\n");
            success = true;
        }
        else
        {
            if (bool result = co_await lockedConnect())
            {
                success = true;
            }
            else
            {
                co_await lockedDisconnect();
            }
        }
        use_sema_.release();

        co_return success;
    }

    IAsyncOperation<bool> send(const IBuffer& msg)
    {
        use_sema_.acquire();
        NamedLog("Attempting to write {} bytes\n", msg.Length());
        auto result = co_await lockedSend(msg);

        use_sema_.release();

        co_return result;
    }

    IAsyncOperation<bool> disconnect()
    {
        try
        {
            use_sema_.acquire();
            auto result = co_await lockedDisconnect();
            use_sema_.release();

            const string ident = identifier_;
            g_callback_queue.enqueue([ident]
            {
                if (g_device_disconnected_callback) {
                    g_device_disconnected_callback(ident.c_str());
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
    g_bluetooth_queue.enqueue([=]
    {
        g_device_found_callback = deviceFoundCallback;
        g_data_received_callback = dataReceivedCallback;
        g_device_connected_callback = deviceConnectedCallback;
        g_device_connection_failed_callback = deviceConnectionFailedCallback;
        g_device_disconnected_callback = deviceDisconnectedCallback;
        g_listener_stopped_callback = listenerStoppedCallback;
    });
}

void godice_set_logger(GDLogger logger)
{
    g_bluetooth_queue.enqueue([logger]
    {
        g_logger = logger;
    });
}

void godice_start_listening()
{
    g_bluetooth_queue.enqueue([]
    {
        if (g_watcher == nullptr)
        {
            g_watcher = BluetoothLEAdvertisementWatcher();
        }
        g_watcher.ScanningMode(BluetoothLEScanningMode::Active);
        g_watcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(k_service_guid);

        auto result = g_watcher.Received(received_device_found_event);

        g_watcher.Stopped([=](auto&&, auto&&)
        {
            log("Watcher Stopped\n");

            g_callback_queue.enqueue([]
            {
                if (g_listener_stopped_callback)
                {
                    g_listener_stopped_callback();
                }
            });
        });

        if (g_device_found_callback)
        {
            // Make a copy of sessions inside the queue
            unordered_map<string, shared_ptr<DeviceSession>> sessions = g_devices_by_identifier;

            g_callback_queue.enqueue([sessions]
            {
                for (const auto& kv : sessions)
                {
                    g_device_found_callback(kv.first.c_str(), kv.second->DeviceName().c_str());
                }
            });
        }

        g_watcher.Start();
    });
}

void godice_connect(const char* inIdent)
{
    string identifier = inIdent;
    g_bluetooth_queue.enqueue([identifier]
    {
        log("Trying to connect to {}\n", identifier);
        bool result = on_queue_internal_connect(identifier).get();
        log("Result was {}\n", result);
    });
}

void godice_disconnect(const char* inIdent)
{
    string identifier = inIdent;
    g_bluetooth_queue.enqueue([identifier]
    {
        shared_ptr<DeviceSession> session = nullptr;

        session = g_devices_by_identifier[identifier];
    
        if (session == nullptr) return;
        
        bool result = session->disconnect().get();
    });
}

void godice_send(const char* id, uint32_t data_size, uint8_t* data)
{
    const string identifier(id);

    const DataWriter writer;
    writer.WriteBytes(winrt::array_view(data, data + data_size));

    const IBuffer buf = writer.DetachBuffer();
    
    g_bluetooth_queue.enqueue([identifier, buf]
    {
        bool success = on_queue_send(identifier, buf).get();
    });
}

static IAsyncOperation<bool> on_queue_send(const string& identifier, const IBuffer& buffer)
{
    const shared_ptr<DeviceSession> session = g_devices_by_identifier[identifier];

    if (session == nullptr)
    {
        log("No session found for {}\n", identifier);
    }
    else
    {
        co_return co_await session->send(buffer);
    }
    co_return false;
}

static void internal_connection_changed_handler(const BluetoothLEDevice& dev, const string& identifier)
{
    g_bluetooth_queue.enqueue([dev, identifier]
    {
        if (dev.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
        {
            log("Got a disconnection event for {}\n", identifier);
            const auto& session = g_devices_by_identifier[identifier];
        
            if (session != nullptr)
            {
                session->disconnect().get();
            }
        }
    });
}

static IAsyncOperation<bool> on_queue_internal_connect(const string& inIdent)
{
    string identifier = inIdent;
    shared_ptr<DeviceSession> session;
    bool success = false;
    
    session = g_devices_by_identifier[identifier];
    
    if (session == nullptr)
    {
        log("No session for {}\n", identifier);
        success = false;
    }
    else
    {
        success = co_await session->Connect();
    }
    
    if (success && g_device_connected_callback)
    {
        g_callback_queue.enqueue([identifier]
        {
            g_device_connected_callback(identifier.c_str());
        });
    }
    if (!success && g_device_connection_failed_callback)
    {
        g_callback_queue.enqueue([identifier]
        {
            g_device_connection_failed_callback(identifier.c_str());
        });
    }
    
    co_return success;
}

static void received_device_found_event(const BluetoothLEAdvertisementWatcher& watcher,
                                     const BluetoothLEAdvertisementReceivedEventArgs& args)
{
    uint64_t btAddr = args.BluetoothAddress();
    string identifier = std::to_string(btAddr);

    g_bluetooth_queue.enqueue([btAddr, identifier]
    {
        on_queue_received_device_found_event(btAddr).get();
    });
}

static IAsyncOperation<bool> on_queue_received_device_found_event(const uint64_t inBtAddr) {
    auto btAddr = inBtAddr;
    
    auto identifier =  std::to_string(btAddr);;
    
    shared_ptr<DeviceSession> session = nullptr;

    bool needsNewSession = true;
    
    if (g_devices_in_progress.contains(identifier))
    {
        needsNewSession = false;
        g_devices_in_progress.insert(identifier);
    }
    else if (g_devices_by_identifier.contains(identifier))
    {
        needsNewSession = false;
    }

    if (needsNewSession)
    {
        session = co_await DeviceSession::MakeSession(btAddr);
        
        if (session != nullptr)
        {
            g_devices_by_identifier.emplace(std::make_pair(identifier, session));
            g_devices_in_progress.erase(identifier);
        }
        else
        {
            log("Failed to create new session\n");
        }
    }
    else
    {
        session = g_devices_by_identifier[identifier];
    }

    if (session && g_device_found_callback)
    {
        g_callback_queue.enqueue([identifier, session]
        {
            g_device_found_callback(identifier.c_str(), session->DeviceName().c_str());
        });
    }
    co_return session != nullptr;
}

void godice_stop_listening()
{
    g_bluetooth_queue.enqueue([]
    {
        g_watcher.Stop();
    });
}

void godice_reset()
{
    g_bluetooth_queue.enqueue([]
    {
        g_watcher.Stop();

        while (g_devices_in_progress.size() > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    
        g_devices_by_identifier.clear();
    });
}

