// GoDiceDll.cpp

#include "GoDiceDll.h"

#include "stdafx.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <winrt/base.h>

#include "WorkQueue.h"

#pragma comment(lib, "windowsapp")

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Storage::Streams;

using Windows::Foundation::IAsyncAction;
using Windows::Foundation::IInspectable;

using std::shared_ptr;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

namespace
{
constexpr auto k_first_notification_timeout = std::chrono::seconds(8);
constexpr uint32_t k_max_reconnect_attempts = 4;
constexpr uint32_t k_max_reconnect_delay_ms = 4000;

constexpr guid k_service_guid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
constexpr guid k_write_guid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
constexpr guid k_notify_guid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

WorkQueue g_callback_queue("CallbackQueue");
WorkQueue g_bluetooth_queue("BluetoothQueue");

std::atomic<GDDeviceFoundCallbackFunction> g_device_found_callback = nullptr;
std::atomic<GDDataCallbackFunction> g_data_received_callback = nullptr;
std::atomic<GDDeviceConnectedCallbackFunction> g_device_connected_callback = nullptr;
std::atomic<GDDeviceConnectionFailedCallbackFunction> g_device_connection_failed_callback = nullptr;
std::atomic<GDDeviceDisconnectedCallbackFunction> g_device_disconnected_callback = nullptr;
std::atomic<GDListenerStoppedCallbackFunction> g_listener_stopped_callback = nullptr;
std::atomic<GDDeviceStateCallbackFunction> g_device_state_callback = nullptr;
std::atomic<GDLogger> g_logger = nullptr;

BluetoothLEAdvertisementWatcher g_watcher = nullptr;
event_token g_watcher_received_token{};
event_token g_watcher_stopped_token{};
bool g_has_watcher_received_token = false;
bool g_has_watcher_stopped_token = false;

class DeviceSession;
unordered_map<string, shared_ptr<DeviceSession>> g_devices_by_identifier;
unordered_set<string> g_devices_in_progress;
std::atomic<uint64_t> g_discovery_generation = 1;

uint64_t monotonic_milliseconds()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void log(const char* message)
{
    if (const auto logger = g_logger.load()) logger(message);
}

template <typename... Args>
void log(string format, Args&&... args)
{
    const auto logger = g_logger.load();
    if (!logger) return;
    const auto message = std::vformat(format, std::make_format_args(args...));
    logger(message.c_str());
}

void emit_device_state(const string& identifier,
                       GDConnectionState state,
                       GDConnectionReason reason,
                       int32_t native_status,
                       string detail)
{
    if (!g_device_state_callback.load()) return;

    const auto timestamp = monotonic_milliseconds();
    g_callback_queue.enqueue([identifier, state, reason, native_status, timestamp, detail = std::move(detail)]
    {
        if (const auto callback = g_device_state_callback.load())
        {
            callback(identifier.c_str(), state, reason, native_status, timestamp, detail.c_str());
        }
    });
}

void emit_device_found(const string& identifier, const string& name)
{
    g_callback_queue.enqueue([identifier, name]
    {
        if (const auto callback = g_device_found_callback.load()) callback(identifier.c_str(), name.c_str());
    });
}

void emit_device_connected(const string& identifier)
{
    g_callback_queue.enqueue([identifier]
    {
        if (const auto callback = g_device_connected_callback.load()) callback(identifier.c_str());
    });
}

void emit_device_connection_failed(const string& identifier)
{
    g_callback_queue.enqueue([identifier]
    {
        if (const auto callback = g_device_connection_failed_callback.load()) callback(identifier.c_str());
    });
}

void emit_device_disconnected(const string& identifier)
{
    g_callback_queue.enqueue([identifier]
    {
        if (const auto callback = g_device_disconnected_callback.load()) callback(identifier.c_str());
    });
}

struct SignalGate
{
    handle event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};

    SignalGate()
    {
        check_pointer(event.get());
    }

    void signal() const noexcept
    {
        if (event) ::SetEvent(event.get());
    }
};

struct ConnectOutcome
{
    bool ready = false;
    int32_t native_status = -1;
    string detail = "connection attempt failed";
};

uint32_t reconnect_delay_ms(uint32_t attempt)
{
    const auto shift = attempt > 0 ? attempt - 1 : 0;
    return (std::min)(500u << (std::min)(shift, 3u), k_max_reconnect_delay_ms);
}

class DeviceSession final : public std::enable_shared_from_this<DeviceSession>
{
public:
    DeviceSession(uint64_t bluetooth_address, string name)
        : bluetooth_address_(bluetooth_address),
          identifier_(std::to_string(bluetooth_address)),
          name_(std::move(name))
    {
    }

    ~DeviceSession()
    {
        close_resources_noexcept();
    }

    const string& identifier() const noexcept { return identifier_; }
    const string& name() const noexcept { return name_; }

    // All request methods run on g_bluetooth_queue.
    void request_connect()
    {
        const auto state = state_.load();
        if (shutting_down_.load() || state == GD_CONNECTION_STATE_READY ||
            state == GD_CONNECTION_STATE_CONNECTING || state == GD_CONNECTION_STATE_SUBSCRIBING ||
            state == GD_CONNECTION_STATE_RETRY_WAIT)
        {
            return;
        }

        desired_connected_ = true;
        reconnect_attempt_ = 0;
        reconnect_reason_ = GD_CONNECTION_REASON_CONNECTION_FAILED;
        begin_new_generation();
        enqueue_connect(0);
    }

    void request_disconnect(GDConnectionReason reason,
                            bool remove_after_disconnect = false,
                            std::function<void()> removed = {})
    {
        desired_connected_ = false;
        if (remove_after_disconnect)
        {
            shutting_down_ = true;
            remove_after_disconnect_ = true;
            removed_callback_ = std::move(removed);
        }
        begin_new_generation();

        state_ = GD_CONNECTION_STATE_DISCONNECTING;
        emit_device_state(identifier_, state_.load(), reason, 0,
                          reason == GD_CONNECTION_REASON_RESET ? "controller reset" : "disconnect requested");
        enqueue_disconnect(reason, false);
    }

    void request_send(vector<uint8_t> bytes)
    {
        if (state_.load() != GD_CONNECTION_STATE_READY)
        {
            log("[{}] refusing write while device is not ready\n", name_);
            return;
        }

        const auto generation = generation_.load();
        enqueue_operation([self = shared_from_this(), generation, bytes = std::move(bytes)]() mutable
        {
            self->send_async(generation, std::move(bytes));
        });
    }

    void handle_link_loss(uint64_t event_generation, int32_t native_status, string detail)
    {
        const auto state = state_.load();
        if (event_generation != generation_.load() || !desired_connected_.load() || shutting_down_.load() ||
            state == GD_CONNECTION_STATE_DISCONNECTING || state == GD_CONNECTION_STATE_DISCONNECTED)
        {
            return;
        }

        log("[{}] link lost: {} ({})\n", name_, detail, native_status);
        reconnect_reason_ = GD_CONNECTION_REASON_LINK_LOSS;
        reconnect_attempt_ = 0;
        begin_new_generation();

        state_ = GD_CONNECTION_STATE_DISCONNECTING;
        emit_device_state(identifier_, state_.load(), GD_CONNECTION_REASON_LINK_LOSS, native_status, detail);

        enqueue_disconnect(GD_CONNECTION_REASON_LINK_LOSS, true);
    }

private:
    using Operation = std::function<void()>;

    const uint64_t bluetooth_address_;
    const string identifier_;
    const string name_;

    std::deque<Operation> operations_;
    bool operation_active_ = false;
    std::atomic_bool desired_connected_ = false;
    std::atomic_bool shutting_down_ = false;
    bool remove_after_disconnect_ = false;
    bool disconnect_pending_ = false;
    bool reconnect_after_disconnect_ = false;
    GDConnectionReason pending_disconnect_reason_ = GD_CONNECTION_REASON_NONE;
    std::function<void()> removed_callback_;
    std::atomic<GDConnectionState> state_ = GD_CONNECTION_STATE_DISCOVERED;
    GDConnectionReason reconnect_reason_ = GD_CONNECTION_REASON_CONNECTION_FAILED;
    uint32_t reconnect_attempt_ = 0;
    std::atomic<uint64_t> generation_ = 1;
    shared_ptr<SignalGate> cancellation_gate_ = std::make_shared<SignalGate>();
    shared_ptr<SignalGate> first_notification_gate_;

    BluetoothLEDevice device_ = nullptr;
    GattSession gatt_session_ = nullptr;
    GattDeviceService service_ = nullptr;
    GattCharacteristic write_characteristic_ = nullptr;
    GattCharacteristic notify_characteristic_ = nullptr;
    GattWriteOption write_option_ = GattWriteOption::WriteWithoutResponse;

    event_token notify_token_{};
    event_token connection_status_token_{};
    event_token session_status_token_{};
    bool has_notify_token_ = false;
    bool has_connection_status_token_ = false;
    bool has_session_status_token_ = false;

    void begin_new_generation()
    {
        cancellation_gate_->signal();
        cancellation_gate_ = std::make_shared<SignalGate>();
        generation_.fetch_add(1);
    }

    bool is_current(uint64_t generation, const shared_ptr<SignalGate>& cancellation) const noexcept
    {
        return generation == generation_.load() &&
               ::WaitForSingleObject(cancellation->event.get(), 0) != WAIT_OBJECT_0;
    }

    void enqueue_operation(Operation operation)
    {
        operations_.push_back(std::move(operation));
        run_next_operation();
    }

    void run_next_operation()
    {
        if (operation_active_ || operations_.empty()) return;

        operation_active_ = true;
        auto operation = std::move(operations_.front());
        operations_.pop_front();
        operation();
    }

    void finish_operation()
    {
        operation_active_ = false;
        run_next_operation();
    }

    void enqueue_connect(uint32_t delay_ms)
    {
        const auto generation = generation_.load();
        const auto cancellation = cancellation_gate_;
        enqueue_operation([self = shared_from_this(), generation, cancellation, delay_ms]
        {
            if (delay_ms == 0)
            {
                self->begin_connect(generation, cancellation);
            }
            else
            {
                self->backoff_async(generation, cancellation, delay_ms);
            }
        });
    }

    void enqueue_disconnect(GDConnectionReason reason, bool reconnect_after_disconnect)
    {
        pending_disconnect_reason_ = reason;
        reconnect_after_disconnect_ = reconnect_after_disconnect;
        if (disconnect_pending_) return;

        disconnect_pending_ = true;
        enqueue_operation([self = shared_from_this()]
        {
            const auto generation = self->generation_.load();
            self->disconnect_async(generation, self->pending_disconnect_reason_);
        });
    }

    fire_and_forget backoff_async(uint64_t generation,
                                  shared_ptr<SignalGate> cancellation,
                                  uint32_t delay_ms)
    {
        co_await resume_background();
        const auto result = ::WaitForSingleObject(cancellation->event.get(), delay_ms);

        g_bluetooth_queue.enqueue([self = shared_from_this(), generation, cancellation, result]
        {
            if (result == WAIT_TIMEOUT && self->is_current(generation, cancellation) &&
                self->desired_connected_.load())
            {
                self->begin_connect(generation, cancellation);
            }
            else
            {
                self->finish_operation();
            }
        });
    }

    void begin_connect(uint64_t generation, const shared_ptr<SignalGate>& cancellation)
    {
        if (!is_current(generation, cancellation) || !desired_connected_.load() || shutting_down_.load())
        {
            finish_operation();
            return;
        }

        state_ = GD_CONNECTION_STATE_CONNECTING;
        emit_device_state(identifier_, state_.load(), GD_CONNECTION_REASON_NONE, 0,
                          reconnect_attempt_ == 0 ? "connection requested" : "reconnect attempt started");
        connect_async(generation, cancellation);
    }

    fire_and_forget connect_async(uint64_t generation, shared_ptr<SignalGate> cancellation)
    {
        ConnectOutcome outcome;

        try
        {
            co_await cleanup_resources_async(false);
            if (!is_current(generation, cancellation)) throw hresult_canceled();

            device_ = co_await BluetoothLEDevice::FromBluetoothAddressAsync(bluetooth_address_);
            if (!device_) throw hresult_error(E_FAIL, L"Bluetooth device is unavailable");
            if (!is_current(generation, cancellation)) throw hresult_canceled();

            const std::weak_ptr<DeviceSession> weak = shared_from_this();
            connection_status_token_ = device_.ConnectionStatusChanged(
                [weak, generation](const BluetoothLEDevice& device, const IInspectable&)
                {
                    if (device.ConnectionStatus() != BluetoothConnectionStatus::Disconnected) return;
                    g_bluetooth_queue.enqueue([weak, generation]
                    {
                        if (const auto self = weak.lock())
                            self->handle_link_loss(generation, 0, "Bluetooth link disconnected");
                    });
                });
            has_connection_status_token_ = true;

            gatt_session_ = co_await GattSession::FromDeviceIdAsync(device_.BluetoothDeviceId());
            if (!gatt_session_) throw hresult_error(E_FAIL, L"Unable to create GATT session");
            if (!is_current(generation, cancellation)) throw hresult_canceled();

            session_status_token_ = gatt_session_.SessionStatusChanged(
                [weak, generation](const GattSession&, const GattSessionStatusChangedEventArgs& args)
                {
                    if (args.Status() != GattSessionStatus::Closed) return;
                    const auto status = static_cast<int32_t>(args.Error());
                    g_bluetooth_queue.enqueue([weak, generation, status]
                    {
                        if (const auto self = weak.lock())
                            self->handle_link_loss(generation, status, "GATT session closed");
                    });
                });
            has_session_status_token_ = true;
            gatt_session_.MaintainConnection(true);

            const auto services_result =
                co_await device_.GetGattServicesForUuidAsync(k_service_guid, BluetoothCacheMode::Uncached);
            if (!is_current(generation, cancellation)) throw hresult_canceled();
            if (services_result.Status() != GattCommunicationStatus::Success || services_result.Services().Size() == 0)
            {
                outcome.native_status = static_cast<int32_t>(services_result.Status());
                throw hresult_error(E_FAIL, L"GoDice GATT service unavailable");
            }
            service_ = services_result.Services().GetAt(0);

            const auto access = co_await service_.RequestAccessAsync();
            if (!is_current(generation, cancellation)) throw hresult_canceled();
            if (access != DeviceAccessStatus::Allowed)
            {
                outcome.native_status = static_cast<int32_t>(access);
                throw hresult_error(E_ACCESSDENIED, L"Access to GoDice GATT service denied");
            }

            const auto notify_result =
                co_await service_.GetCharacteristicsForUuidAsync(k_notify_guid, BluetoothCacheMode::Uncached);
            if (!is_current(generation, cancellation)) throw hresult_canceled();
            if (notify_result.Status() != GattCommunicationStatus::Success || notify_result.Characteristics().Size() == 0)
            {
                outcome.native_status = static_cast<int32_t>(notify_result.Status());
                throw hresult_error(E_FAIL, L"GoDice notification characteristic unavailable");
            }
            notify_characteristic_ = notify_result.Characteristics().GetAt(0);
            if ((notify_characteristic_.CharacteristicProperties() & GattCharacteristicProperties::Notify) ==
                GattCharacteristicProperties::None)
            {
                throw hresult_error(E_FAIL, L"GoDice characteristic does not support notifications");
            }

            const auto write_result =
                co_await service_.GetCharacteristicsForUuidAsync(k_write_guid, BluetoothCacheMode::Uncached);
            if (!is_current(generation, cancellation)) throw hresult_canceled();
            if (write_result.Status() != GattCommunicationStatus::Success || write_result.Characteristics().Size() == 0)
            {
                outcome.native_status = static_cast<int32_t>(write_result.Status());
                throw hresult_error(E_FAIL, L"GoDice write characteristic unavailable");
            }
            write_characteristic_ = write_result.Characteristics().GetAt(0);
            const auto write_properties = write_characteristic_.CharacteristicProperties();
            if ((write_properties & GattCharacteristicProperties::WriteWithoutResponse) != GattCharacteristicProperties::None)
            {
                write_option_ = GattWriteOption::WriteWithoutResponse;
            }
            else if ((write_properties & GattCharacteristicProperties::Write) != GattCharacteristicProperties::None)
            {
                write_option_ = GattWriteOption::WriteWithResponse;
            }
            else
            {
                throw hresult_error(E_FAIL, L"GoDice characteristic is not writable");
            }

            if (!is_current(generation, cancellation)) throw hresult_canceled();

            first_notification_gate_ = std::make_shared<SignalGate>();
            const auto notification_gate = first_notification_gate_;
            notify_token_ = notify_characteristic_.ValueChanged(
                [weak, generation, notification_gate](const GattCharacteristic&,
                                                       const GattValueChangedEventArgs& args)
                {
                    notification_gate->signal();
                    if (const auto self = weak.lock()) self->forward_data(generation, args.CharacteristicValue());
                });
            has_notify_token_ = true;

            state_ = GD_CONNECTION_STATE_SUBSCRIBING;
            emit_device_state(identifier_, state_.load(), GD_CONNECTION_REASON_NONE, 0,
                              "enabling notifications and waiting for first notification");
            const auto config_status = co_await notify_characteristic_
                .WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::Notify);
            if (!is_current(generation, cancellation)) throw hresult_canceled();
            if (config_status != GattCommunicationStatus::Success)
            {
                outcome.native_status = static_cast<int32_t>(config_status);
                throw hresult_error(E_FAIL, L"Failed to enable GoDice notifications");
            }

            co_await resume_background();
            HANDLE wait_handles[] = { notification_gate->event.get(), cancellation->event.get() };
            const auto wait_result = ::WaitForMultipleObjects(
                2,
                wait_handles,
                FALSE,
                static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    k_first_notification_timeout).count()));

            if (wait_result == WAIT_OBJECT_0 + 1 || !is_current(generation, cancellation))
                throw hresult_canceled();
            if (wait_result != WAIT_OBJECT_0)
                throw hresult_error(HRESULT_FROM_WIN32(WAIT_TIMEOUT), L"Timed out waiting for first GoDice notification");
            if (device_.ConnectionStatus() != BluetoothConnectionStatus::Connected)
                throw hresult_error(E_FAIL, L"Bluetooth link dropped during connection setup");

            outcome.ready = true;
            outcome.native_status = 0;
            outcome.detail = "notification subscription ready";
        }
        catch (const hresult_canceled&)
        {
            outcome.detail = "connection attempt canceled";
        }
        catch (const hresult_error& error)
        {
            outcome.native_status = error.code().value;
            outcome.detail = to_string(error.message());
        }
        catch (const std::exception& error)
        {
            outcome.detail = error.what();
        }
        catch (...)
        {
            outcome.detail = "unknown connection exception";
        }

        if (!outcome.ready) co_await cleanup_resources_async(false);

        g_bluetooth_queue.enqueue([self = shared_from_this(), generation, cancellation, outcome = std::move(outcome)]() mutable
        {
            self->finish_connect(generation, cancellation, std::move(outcome));
        });
    }

    void finish_connect(uint64_t generation,
                        const shared_ptr<SignalGate>& cancellation,
                        ConnectOutcome outcome)
    {
        if (!is_current(generation, cancellation) || !desired_connected_.load() || shutting_down_.load())
        {
            finish_operation();
            return;
        }

        if (outcome.ready)
        {
            state_ = GD_CONNECTION_STATE_READY;
            reconnect_attempt_ = 0;
            emit_device_state(identifier_, state_.load(), GD_CONNECTION_REASON_NONE, 0, outcome.detail);
            emit_device_connected(identifier_);
        }
        else if (reconnect_attempt_ < k_max_reconnect_attempts)
        {
            ++reconnect_attempt_;
            const auto delay = reconnect_delay_ms(reconnect_attempt_);
            state_ = GD_CONNECTION_STATE_RETRY_WAIT;
            emit_device_state(identifier_, state_.load(), reconnect_reason_, outcome.native_status,
                              std::format("{}; retry {}/{} in {} ms",
                                          outcome.detail,
                                          reconnect_attempt_,
                                          k_max_reconnect_attempts,
                                          delay));
            enqueue_connect(delay);
        }
        else
        {
            desired_connected_ = false;
            state_ = GD_CONNECTION_STATE_DISCONNECTED;
            emit_device_state(identifier_, state_.load(), reconnect_reason_,
                              outcome.native_status, outcome.detail);
            emit_device_connection_failed(identifier_);
        }

        finish_operation();
    }

    fire_and_forget disconnect_async(uint64_t generation, GDConnectionReason reason)
    {
        int32_t status = 0;
        string detail = reason == GD_CONNECTION_REASON_LINK_LOSS ? "link-loss cleanup complete" : "disconnected";

        try
        {
            co_await cleanup_resources_async(reason == GD_CONNECTION_REASON_REQUESTED);
        }
        catch (const hresult_error& error)
        {
            status = error.code().value;
            detail = to_string(error.message());
        }
        catch (...)
        {
            status = -1;
            detail = "disconnect cleanup failed";
        }

        g_bluetooth_queue.enqueue([self = shared_from_this(), generation, status,
                                   detail = std::move(detail)]() mutable
        {
            self->finish_disconnect(generation, status, std::move(detail));
        });
    }

    void finish_disconnect(uint64_t generation,
                           int32_t native_status,
                           string detail)
    {
        const auto reason = pending_disconnect_reason_;
        const auto reconnect_after_disconnect = reconnect_after_disconnect_;
        disconnect_pending_ = false;
        reconnect_after_disconnect_ = false;
        pending_disconnect_reason_ = GD_CONNECTION_REASON_NONE;

        state_ = GD_CONNECTION_STATE_DISCONNECTED;
        emit_device_state(identifier_, state_.load(), reason, native_status, std::move(detail));
        emit_device_disconnected(identifier_);

        if (reconnect_after_disconnect && desired_connected_.load() && !shutting_down_.load() &&
            generation == generation_.load())
        {
            reconnect_attempt_ = 1;
            const auto delay = reconnect_delay_ms(reconnect_attempt_);
            state_ = GD_CONNECTION_STATE_RETRY_WAIT;
            emit_device_state(identifier_, state_.load(), GD_CONNECTION_REASON_LINK_LOSS, native_status,
                              std::format("retry {}/{} in {} ms", reconnect_attempt_,
                                          k_max_reconnect_attempts, delay));
            enqueue_connect(delay);
        }

        finish_operation();

        if (remove_after_disconnect_)
        {
            remove_after_disconnect_ = false;
            if (removed_callback_) removed_callback_();
            removed_callback_ = {};
        }
    }

    fire_and_forget send_async(uint64_t generation, vector<uint8_t> bytes)
    {
        bool success = false;
        int32_t native_status = 0;

        try
        {
            if (generation == generation_.load() && state_.load() == GD_CONNECTION_STATE_READY && write_characteristic_)
            {
                DataWriter writer;
                writer.WriteBytes(bytes);
                const auto buffer = writer.DetachBuffer();
                const auto result = co_await write_characteristic_.WriteValueAsync(buffer, write_option_);
                native_status = static_cast<int32_t>(result);
                success = result == GattCommunicationStatus::Success;
            }
        }
        catch (const hresult_error& error)
        {
            native_status = error.code().value;
        }
        catch (...)
        {
            native_status = -1;
        }

        g_bluetooth_queue.enqueue([self = shared_from_this(), generation, success, native_status]
        {
            if (!success && generation == self->generation_.load() &&
                self->state_.load() == GD_CONNECTION_STATE_READY)
            {
                self->handle_link_loss(generation, native_status, "GATT write failed");
            }
            self->finish_operation();
        });
    }

    IAsyncAction cleanup_resources_async(bool disable_notifications)
    {
        if (notify_characteristic_ && has_notify_token_)
        {
            try { notify_characteristic_.ValueChanged(std::exchange(notify_token_, {})); } catch (...) {}
            has_notify_token_ = false;
        }
        first_notification_gate_.reset();

        if (disable_notifications && notify_characteristic_)
        {
            try
            {
                co_await notify_characteristic_.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::None);
            }
            catch (...) {}
        }

        if (device_ && has_connection_status_token_)
        {
            try { device_.ConnectionStatusChanged(std::exchange(connection_status_token_, {})); } catch (...) {}
            has_connection_status_token_ = false;
        }
        if (gatt_session_ && has_session_status_token_)
        {
            try { gatt_session_.SessionStatusChanged(std::exchange(session_status_token_, {})); } catch (...) {}
            has_session_status_token_ = false;
        }

        write_characteristic_ = nullptr;
        notify_characteristic_ = nullptr;
        if (service_)
        {
            try { service_.Close(); } catch (...) {}
            service_ = nullptr;
        }
        if (gatt_session_)
        {
            try
            {
                gatt_session_.MaintainConnection(false);
                gatt_session_.Close();
            }
            catch (...) {}
            gatt_session_ = nullptr;
        }
        if (device_)
        {
            try { device_.Close(); } catch (...) {}
            device_ = nullptr;
        }
    }

    void close_resources_noexcept() noexcept
    {
        try
        {
            if (notify_characteristic_ && has_notify_token_)
                notify_characteristic_.ValueChanged(notify_token_);
            if (device_ && has_connection_status_token_)
                device_.ConnectionStatusChanged(connection_status_token_);
            if (gatt_session_ && has_session_status_token_)
                gatt_session_.SessionStatusChanged(session_status_token_);
            if (service_) service_.Close();
            if (gatt_session_)
            {
                gatt_session_.MaintainConnection(false);
                gatt_session_.Close();
            }
            if (device_) device_.Close();
        }
        catch (...) {}
    }

    void forward_data(uint64_t generation, const IBuffer& buffer) const
    {
        if (generation != generation_.load() || state_.load() != GD_CONNECTION_STATE_READY ||
            !g_data_received_callback.load())
            return;

        vector<uint8_t> bytes(buffer.Length());
        if (!bytes.empty()) std::copy_n(buffer.data(), bytes.size(), bytes.data());
        const auto identifier = identifier_;
        g_callback_queue.enqueue([identifier, bytes = std::move(bytes)]
        {
            if (const auto callback = g_data_received_callback.load())
                callback(identifier.c_str(), static_cast<uint32_t>(bytes.size()),
                         const_cast<uint8_t*>(bytes.data()));
        });
    }
};

fire_and_forget discover_device_async(uint64_t bluetooth_address, uint64_t discovery_generation)
{
    string name;
    int32_t native_status = 0;

    try
    {
        const auto device = co_await BluetoothLEDevice::FromBluetoothAddressAsync(bluetooth_address);
        if (!device) throw hresult_error(E_FAIL, L"Bluetooth device is unavailable");
        name = to_string(device.Name());
        device.Close();
    }
    catch (const hresult_error& error)
    {
        native_status = error.code().value;
        log("Failed to create session for {}: {}\n", bluetooth_address, to_string(error.message()));
    }
    catch (...)
    {
        native_status = -1;
    }

    const auto identifier = std::to_string(bluetooth_address);
    g_bluetooth_queue.enqueue([bluetooth_address, discovery_generation, identifier,
                               name = std::move(name), native_status]() mutable
    {
        g_devices_in_progress.erase(identifier);
        if (discovery_generation != g_discovery_generation.load()) return;

        if (native_status != 0)
        {
            emit_device_state(identifier, GD_CONNECTION_STATE_DISCONNECTED,
                              GD_CONNECTION_REASON_CONNECTION_FAILED, native_status,
                              "failed to create device session");
            return;
        }

        auto [iterator, inserted] = g_devices_by_identifier.emplace(
            identifier,
            std::make_shared<DeviceSession>(bluetooth_address, name.empty() ? identifier : name));
        if (!inserted) return;

        emit_device_state(identifier, GD_CONNECTION_STATE_DISCOVERED,
                          GD_CONNECTION_REASON_NONE, 0, "device discovered");
        emit_device_found(identifier, iterator->second->name());
    });
}

void received_device_found_event(const BluetoothLEAdvertisementWatcher&,
                                 const BluetoothLEAdvertisementReceivedEventArgs& args)
{
    const auto bluetooth_address = args.BluetoothAddress();
    const auto identifier = std::to_string(bluetooth_address);
    g_bluetooth_queue.enqueue([bluetooth_address, identifier]
    {
        if (g_devices_by_identifier.contains(identifier) || g_devices_in_progress.contains(identifier)) return;

        g_devices_in_progress.insert(identifier);
        discover_device_async(bluetooth_address, g_discovery_generation.load());
    });
}

void create_watcher_if_needed()
{
    if (g_watcher) return;

    g_watcher = BluetoothLEAdvertisementWatcher();
    g_watcher.ScanningMode(BluetoothLEScanningMode::Active);
    g_watcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(k_service_guid);
    g_watcher_received_token = g_watcher.Received(received_device_found_event);
    g_has_watcher_received_token = true;
    g_watcher_stopped_token = g_watcher.Stopped([](const auto&, const auto&)
    {
        g_callback_queue.enqueue([]
        {
            if (const auto callback = g_listener_stopped_callback.load()) callback();
        });
    });
    g_has_watcher_stopped_token = true;
}

void stop_and_release_watcher()
{
    if (!g_watcher) return;

    try
    {
        if (g_watcher.Status() == BluetoothLEAdvertisementWatcherStatus::Started) g_watcher.Stop();
        if (g_has_watcher_received_token) g_watcher.Received(std::exchange(g_watcher_received_token, {}));
        if (g_has_watcher_stopped_token) g_watcher.Stopped(std::exchange(g_watcher_stopped_token, {}));
    }
    catch (...) {}

    g_has_watcher_received_token = false;
    g_has_watcher_stopped_token = false;
    g_watcher = nullptr;
}
} // namespace

void godice_set_callbacks(GDDeviceFoundCallbackFunction device_found_callback,
                          GDDataCallbackFunction data_received_callback,
                          GDDeviceConnectedCallbackFunction device_connected_callback,
                          GDDeviceConnectionFailedCallbackFunction device_connection_failed_callback,
                          GDDeviceDisconnectedCallbackFunction device_disconnected_callback,
                          GDListenerStoppedCallbackFunction listener_stopped_callback)
{
    g_bluetooth_queue.enqueue([=]
    {
        g_device_found_callback = device_found_callback;
        g_data_received_callback = data_received_callback;
        g_device_connected_callback = device_connected_callback;
        g_device_connection_failed_callback = device_connection_failed_callback;
        g_device_disconnected_callback = device_disconnected_callback;
        g_listener_stopped_callback = listener_stopped_callback;
    });
}

void godice_set_device_state_callback(GDDeviceStateCallbackFunction device_state_callback)
{
    g_bluetooth_queue.enqueue([device_state_callback]
    {
        g_device_state_callback = device_state_callback;
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
        create_watcher_if_needed();

        for (const auto& [identifier, session] : g_devices_by_identifier)
            emit_device_found(identifier, session->name());

        if (g_watcher.Status() != BluetoothLEAdvertisementWatcherStatus::Started)
            g_watcher.Start();
    });
}

void godice_stop_listening()
{
    g_bluetooth_queue.enqueue([]
    {
        if (g_watcher && g_watcher.Status() == BluetoothLEAdvertisementWatcherStatus::Started)
            g_watcher.Stop();
    });
}

void godice_connect(const char* identifier_pointer)
{
    if (!identifier_pointer) return;
    const string identifier(identifier_pointer);
    g_bluetooth_queue.enqueue([identifier]
    {
        const auto iterator = g_devices_by_identifier.find(identifier);
        if (iterator == g_devices_by_identifier.end())
        {
            emit_device_state(identifier, GD_CONNECTION_STATE_DISCONNECTED,
                              GD_CONNECTION_REASON_CONNECTION_FAILED, -1,
                              "no discovered session");
            emit_device_connection_failed(identifier);
            return;
        }
        iterator->second->request_connect();
    });
}

void godice_disconnect(const char* identifier_pointer)
{
    if (!identifier_pointer) return;
    const string identifier(identifier_pointer);
    g_bluetooth_queue.enqueue([identifier]
    {
        const auto iterator = g_devices_by_identifier.find(identifier);
        if (iterator == g_devices_by_identifier.end())
        {
            emit_device_state(identifier, GD_CONNECTION_STATE_DISCONNECTED,
                              GD_CONNECTION_REASON_REQUESTED, -1,
                              "no discovered session");
            return;
        }
        iterator->second->request_disconnect(GD_CONNECTION_REASON_REQUESTED);
    });
}

void godice_send(const char* identifier_pointer, uint32_t data_size, uint8_t* data)
{
    if (!identifier_pointer || (data_size > 0 && !data)) return;
    const string identifier(identifier_pointer);
    vector<uint8_t> bytes;
    if (data_size > 0) bytes.assign(data, data + data_size);
    g_bluetooth_queue.enqueue([identifier, bytes = std::move(bytes)]() mutable
    {
        const auto iterator = g_devices_by_identifier.find(identifier);
        if (iterator == g_devices_by_identifier.end())
        {
            log("No session found for {}\n", identifier);
            return;
        }
        iterator->second->request_send(std::move(bytes));
    });
}

void godice_reset()
{
    g_bluetooth_queue.enqueue([]
    {
        g_discovery_generation.fetch_add(1);
        g_devices_in_progress.clear();
        stop_and_release_watcher();

        vector<shared_ptr<DeviceSession>> sessions;
        sessions.reserve(g_devices_by_identifier.size());
        for (const auto& [identifier, session] : g_devices_by_identifier) sessions.push_back(session);

        for (const auto& session : sessions)
        {
            const auto identifier = session->identifier();
            const std::weak_ptr<DeviceSession> weak = session;
            session->request_disconnect(GD_CONNECTION_REASON_RESET, true, [identifier, weak]
            {
                const auto iterator = g_devices_by_identifier.find(identifier);
                if (iterator != g_devices_by_identifier.end() && iterator->second == weak.lock())
                    g_devices_by_identifier.erase(iterator);
            });
        }
    });
}
