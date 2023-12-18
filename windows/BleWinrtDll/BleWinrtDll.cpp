// BleWinrtDll.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
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

static std::mutex gMapMutex;
static std::map<string, shared_ptr<DeviceSession>> gDevicesByIdentifier;

static inline constexpr guid kServiceGuid = guid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kWriteGuid = guid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kNotifyGuid = guid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

static fire_and_forget ReceivedEvent(const BluetoothLEAdvertisementWatcher& watcher, const BluetoothLEAdvertisementReceivedEventArgs& args);
static fire_and_forget internalConnect(const string& identifier);

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

	void SetNotifyCharacteristic(const GattCharacteristic& nCh) {
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

	void SetWriteCharacteristic(const GattCharacteristic& wCh) {
		write_characteristic_ = wCh;
	}

	const shared_ptr<BluetoothLEDevice> GetDevice() { return device_; }

	auto GetWriteCharacteristic() const -> const GattCharacteristic& {
		return write_characteristic_;
	}

	~DeviceSession() {
		if (notify_characteristic_) {
			notify_characteristic_.ValueChanged([](auto&& ch, auto&& args) {});
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

void godice_start_listening()
{
	if (gWatcher == nullptr)
	{
		gWatcher = BluetoothLEAdvertisementWatcher();
	}
	gWatcher.ScanningMode(BluetoothLEScanningMode::Active);
	gWatcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(kServiceGuid);

	gWatcher.Received(ReceivedEvent);
	
	gWatcher.Stopped([=](auto &&, auto &&)
	{
		cout << "Stopped";
		godice_stop_listening();
	});

	gWatcher.Start();
}

void godice_connect(const char* identifier) {
	internalConnect(string(identifier));
}

void godice_disconnect(const char* identifier) {
	auto session = gDevicesByIdentifier[identifier];
	gDevicesByIdentifier.erase(identifier);
}

void godice_send(const char *identifier, uint32_t data_size, uint8_t *data){
	auto session = gDevicesByIdentifier[identifier];
	
	auto buf = Buffer(data_size);

	memcpy(buf.data(), data, data_size);

	session->GetWriteCharacteristic().WriteValueAsync(buf);
}

static fire_and_forget internalConnect(const string& identifier) {
	auto session = gDevicesByIdentifier[identifier];
	auto& device = session->GetDevice();
	
	device->ConnectionStatusChanged([=](const BluetoothLEDevice& dev, auto&& args)
		{
			if (dev.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
			{
				if (gDeviceDisconnectedCallback) {
					gDeviceDisconnectedCallback(identifier.c_str());
				}
			}
		});
	
	// Try co_awaiting to get the services and characteristics
	const auto services = (co_await device->GetGattServicesForUuidAsync(kServiceGuid)).Services();

	for (const GattDeviceService& svc : services)
	{
		if (svc.Uuid() != kServiceGuid) continue;

		auto nChResult = co_await svc.GetCharacteristicsForUuidAsync(kNotifyGuid);

		GattCharacteristic nCh = nChResult.Characteristics().GetAt(0);

		session->SetNotifyCharacteristic(nCh);

		auto writeConfigStatus = co_await nCh.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify);

		if (writeConfigStatus == GattCommunicationStatus::Success)
		{
			auto wChResult = co_await svc.GetCharacteristicsForUuidAsync(kWriteGuid);
			GattCharacteristic wCh = wChResult.Characteristics().GetAt(0);

			session->SetWriteCharacteristic(wCh);

			if (gDeviceConnectedCallback) {
				gDeviceConnectedCallback(identifier.c_str());
			}
			
			co_return;
		}
		else
		{
			throw exception("oops");
		}
	}
}

static fire_and_forget ReceivedEvent(const BluetoothLEAdvertisementWatcher &watcher, const BluetoothLEAdvertisementReceivedEventArgs &args) {
	const BluetoothLEAdvertisement& ad = args.Advertisement();

	shared_ptr<BluetoothLEDevice> device = nullptr;
	shared_ptr<DeviceSession> session = nullptr;

	uint64_t btAddr = args.BluetoothAddress();
	string identifier = to_string(btAddr);
	{
		std::scoped_lock<std::mutex> lock(gMapMutex);
		if (gDevicesByIdentifier.count(identifier) > 0) {
			co_return;
		}
	}
	
	Windows::Foundation::IAsyncOperation<BluetoothLEDevice> deviceAsync = BluetoothLEDevice::FromBluetoothAddressAsync(btAddr);
	device = std::make_shared<BluetoothLEDevice>(co_await deviceAsync);

	{
		std::scoped_lock<std::mutex> lock(gMapMutex);
		if (gDevicesByIdentifier.count(identifier) > 0) {
			co_return;
		}

		session = std::make_shared<DeviceSession>(device, identifier);
		gDevicesByIdentifier[identifier] = session;

		if (gDeviceFoundCallback) {
			gDeviceFoundCallback(identifier.c_str(), to_string(device->Name()).c_str());
		}
	}
}

void godice_stop_listening()
{
	const std::scoped_lock<std::mutex> lock(gMapMutex);
	gWatcher.Stop();
	gDevicesByIdentifier.clear();
}
