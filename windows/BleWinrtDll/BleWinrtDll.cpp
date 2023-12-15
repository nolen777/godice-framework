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

static GDVectorCallbackFunction gDataReceivedCallback = nullptr;
static std::mutex gMapMutex;
static std::map<uint64_t, shared_ptr<DeviceSession>> gDevicesByAddress;

static inline constexpr guid kServiceGuid = guid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kWriteGuid = guid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kNotifyGuid = guid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");


static fire_and_forget ReceivedEvent(const BluetoothLEAdvertisementWatcher& watcher, const BluetoothLEAdvertisementReceivedEventArgs& args);

static void MaybeSend(const char* txt, int32_t byteCount, uint8_t* bytes) {
	if (gDataReceivedCallback != nullptr)
	{
		gDataReceivedCallback(txt, byteCount, bytes);
	}
}

static void MaybeSend(const shared_ptr<BluetoothLEDevice>& dev, int32_t byteCount, uint8_t* bytes) {
	MaybeSend(to_string(dev->Name()).c_str(), byteCount, bytes);
}

struct DeviceSession
{
private:
	shared_ptr<BluetoothLEDevice> device_;
	GattCharacteristic write_characteristic_ = nullptr;
	GattCharacteristic notify_characteristic_ = nullptr;
	
public:
	DeviceSession(const shared_ptr<BluetoothLEDevice>& dev) : device_(dev)
	{
	}

	void SetNotifyCharacteristic(const GattCharacteristic& nCh) {
		notify_characteristic_ = nCh;

		notify_characteristic_.ValueChanged([=](auto&& ch, auto&& args)
			{
				const IBuffer& data = args.CharacteristicValue();
				MaybeSend(device_, data.Length(), data.data());
			});
	}

	void SetWriteCharacteristic(const GattCharacteristic& wCh) {
		write_characteristic_ = wCh;
	}

	~DeviceSession() {
		if (notify_characteristic_) {
			notify_characteristic_.ValueChanged([](auto&& ch, auto&& args) {});
		}
	}
};

void godice_set_callback(GDVectorCallbackFunction callback)
{
	gDataReceivedCallback = callback;
}


void godice_start_listening()
{
	//MaybeSend("Start listening AWXYZ", 0, nullptr);
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

static fire_and_forget ReceivedEvent(const BluetoothLEAdvertisementWatcher &watcher, const BluetoothLEAdvertisementReceivedEventArgs &args) {
//	MaybeSend("Received", 0, nullptr);
	const BluetoothLEAdvertisement& ad = args.Advertisement();

	shared_ptr<BluetoothLEDevice> device = nullptr;
	shared_ptr<DeviceSession> session = nullptr;
	string deviceName = "";

	uint64_t btAddr = args.BluetoothAddress();
	{
		std::scoped_lock<std::mutex> lock(gMapMutex);
		if (gDevicesByAddress.count(btAddr) > 0) {
//			MaybeSend("Already have this address, skipping", 0, nullptr);
			co_return;
		}
	}
	
	Windows::Foundation::IAsyncOperation<BluetoothLEDevice> deviceAsync = BluetoothLEDevice::FromBluetoothAddressAsync(args.BluetoothAddress());
	device = std::make_shared<BluetoothLEDevice>(co_await deviceAsync);

	{
		std::scoped_lock<std::mutex> lock(gMapMutex);
		if (gDevicesByAddress.count(btAddr) > 0) {
//			MaybeSend("Already have this address 2, skipping", 0, nullptr);
			co_return;
		}

		session = std::make_shared<DeviceSession>(device);
		gDevicesByAddress[btAddr] = session;
		deviceName = to_string(device->Name());
	}

	MaybeSend(device, 0, nullptr);

	// Try co_awaiting to get the services and characteristics
	const auto services = (co_await device->GetGattServicesForUuidAsync(kServiceGuid)).Services();

	for (const GattDeviceService& svc : services)
	{
	//	MaybeSend("Got services", 0, nullptr);
		if (svc.Uuid() != kServiceGuid) continue;

		auto nChResult = co_await svc.GetCharacteristicsForUuidAsync(kNotifyGuid);

	//	MaybeSend("Got notify characteristic", 0, nullptr);
		GattCharacteristic nCh = nChResult.Characteristics().GetAt(0);

		session->SetNotifyCharacteristic(nCh);

		auto writeConfigStatus = co_await nCh.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify);

		if (writeConfigStatus == GattCommunicationStatus::Success)
		{
			auto wChResult = co_await svc.GetCharacteristicsForUuidAsync(kWriteGuid);
	//		MaybeSend("Got write characteristic", 0, nullptr);
			GattCharacteristic wCh = wChResult.Characteristics().GetAt(0);

			session->SetWriteCharacteristic(wCh);

			MaybeSend(device, 0, nullptr);

		}
		else
		{
			throw exception("oops");
		}
	}
}

void godice_stop_listening()
{
	const std::scoped_lock<std::mutex> lock(gMapMutex);
	gWatcher.Stop();
	gDevicesByAddress.clear();
}
