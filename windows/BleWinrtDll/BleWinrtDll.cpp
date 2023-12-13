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
static std::map<string, shared_ptr<DeviceSession>> gDevices;

static inline constexpr guid kServiceGuid = guid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kWriteGuid = guid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static inline constexpr guid kNotifyGuid = guid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

static void MaybeSend(const char* txt, int32_t byteCount, uint8_t* bytes) {
	if (gDataReceivedCallback != nullptr)
	{
		gDataReceivedCallback(txt, byteCount, bytes);
	}
}

static void MaybeSend(const shared_ptr<BluetoothLEDevice>& dev, int32_t byteCount, uint8_t* bytes) {
	MaybeSend(to_string(dev->Name()).c_str(), byteCount, bytes);
}

class DeviceSession
{
private:
	shared_ptr<BluetoothLEDevice> device_;
	GattCharacteristic write_characteristic_;
	GattCharacteristic notify_characteristic_;
	
public:
	DeviceSession(const shared_ptr<BluetoothLEDevice>& dev, const GattCharacteristic& wCh, const GattCharacteristic nCh) : device_(dev), write_characteristic_(wCh), notify_characteristic_(nCh)
	{
		notify_characteristic_.ValueChanged([=](auto&& ch, auto&& args)
		{
			const IBuffer& data = args.CharacteristicValue();
			MaybeSend(dev, data.Length(), data.data());
		});
	}
};

void godice_set_callback(GDVectorCallbackFunction callback)
{
	gDataReceivedCallback = callback;
}


void godice_start_listening()
{
	MaybeSend("Start listening WXYZ", 0, nullptr);
	if (gWatcher == nullptr)
	{
		gWatcher = BluetoothLEAdvertisementWatcher();
	}
	gWatcher.ScanningMode(BluetoothLEScanningMode::Active);
	gWatcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(kServiceGuid);

	gWatcher.Received([=](auto && watcher, auto && args) -> winrt::fire_and_forget {
		MaybeSend("Received", 0, nullptr);
		const BluetoothLEAdvertisement& ad = args.Advertisement();

		Windows::Foundation::IAsyncOperation<BluetoothLEDevice> deviceAsync = BluetoothLEDevice::FromBluetoothAddressAsync(args.BluetoothAddress());
		auto device = std::make_shared<BluetoothLEDevice>(co_await deviceAsync);
	
		// Try co_awaiting to get the services and characteristics
		const auto services = (co_await device->GetGattServicesForUuidAsync(kServiceGuid)).Services();

		for (const GattDeviceService& svc : services)
		{
			MaybeSend("Got services", 0, nullptr);
			if (svc.Uuid() != kServiceGuid) continue;

			auto nChResult = co_await svc.GetCharacteristicsForUuidAsync(kNotifyGuid);

			MaybeSend("Got notify characteristic", 0, nullptr);
			GattCharacteristic nCh = nChResult.Characteristics().GetAt(0);

			auto writeConfigStatus = co_await nCh.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify);

			if (writeConfigStatus == GattCommunicationStatus::Success)
			{
				auto wChResult = co_await svc.GetCharacteristicsForUuidAsync(kWriteGuid);
				MaybeSend("Got write characteristic", 0, nullptr);
				GattCharacteristic wCh = wChResult.Characteristics().GetAt(0);

				MaybeSend(device, 0, nullptr);

				std::lock_guard<std::mutex> lock(gMapMutex);
				gDevices[to_string(device->Name())] = std::make_shared<DeviceSession>(device, wCh, nCh);
				break;
			}
			else
			{
				throw exception("oops");
			}
		}
	});
	
	gWatcher.Stopped([=](auto &&, auto &&)
	{
		cout << "Stopped";
		godice_stop_listening();
	});

	gWatcher.Start();
}

static void ReceivedEvent(BluetoothLEAdvertisementWatcher &&watcher, BluetoothLEAdvertisementReceivedEventArgs &&args) {

}

void godice_stop_listening()
{
	const std::lock_guard<std::mutex> lock(gMapMutex);
	gWatcher.Stop();
	gDevices.clear();
}
