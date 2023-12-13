// BleWinrtDll.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
//

#include "stdafx.h"
#include <winrt/windows.foundation.h>

#include "BleWinrtDll.h"

#include <ppltasks.h>

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

using concurrency::task;

static BluetoothLEAdvertisementWatcher gWatcher = nullptr;

const static guid kServiceGuid = guid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");

class DeviceSession;

static GDVectorCallbackFunction gDataReceivedCallback = nullptr;
static std::map<string, std::shared_ptr<DeviceSession>> gDevices;

class DeviceSession
{
private:
	std::shared_ptr<BluetoothLEDevice> device_;
	GattCharacteristic write_characteristic_;
	GattCharacteristic notify_characteristic_;

	static inline constexpr guid kWriteGuid = guid("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
	static inline constexpr guid kNotifyGuid = guid("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
	
public:
	static task<std::shared_ptr<DeviceSession>> CreateSession(const std::shared_ptr<BluetoothLEDevice>& dev)
	{
		const auto serviceAsync = co_await dev->GetGattServicesAsync();
	//	auto awaited = co_await serviceAsync;
		//const auto services = (co_await serviceAsync).Services();
		const auto services = serviceAsync.get().Services();

		for (const GattDeviceService& svc : services)
		{
			if (svc.Uuid() != kServiceGuid) continue;


			auto nChAsync = svc.GetCharacteristicsForUuidAsync(kNotifyGuid);
			co_await nChAsync;
			GattCharacteristic nCh = nChAsync.get().Characteristics().GetAt(0);

			auto writeConfigAsync = nCh.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify);
			co_await writeConfigAsync;

			if (writeConfigAsync.get() == GattCommunicationStatus::Success)
			{
				auto wChAsync = svc.GetCharacteristicsForUuidAsync(kWriteGuid);
				co_await wChAsync;
				GattCharacteristic wCh = wChAsync.get().Characteristics().GetAt(0);

				co_return concurrency::create_task(std::make_shared<DeviceSession>(dev, wCh, nCh));
			}
			else
			{
				throw exception("oops");
			}
		}

		throw exception("No services discovered");
	}
	
	DeviceSession(const std::shared_ptr<BluetoothLEDevice>& dev, const GattCharacteristic& wCh, const GattCharacteristic nCh) : device_(dev), write_characteristic_(wCh), notify_characteristic_(nCh)
	{
		notify_characteristic_.ValueChanged([=](auto&& ch, auto&& args)
		{
			if (gDataReceivedCallback != nullptr)
			{
				const IBuffer& data = args.CharacteristicValue();
				gDataReceivedCallback(to_string(device_->Name()).c_str(), data.Length(),  data.data());
			}
		});
	}
};

void godice_set_callback(GDVectorCallbackFunction callback)
{
	gDataReceivedCallback = callback;
}


void godice_start_listening()
{
	if (gWatcher == nullptr)
	{
		gWatcher = BluetoothLEAdvertisementWatcher();
	}
	gWatcher.ScanningMode(BluetoothLEScanningMode::Active);
	gWatcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(kServiceGuid);

	gWatcher.Received([=](auto && watcher, auto && args) -> winrt::fire_and_forget {
		cout << "Received" << std::endl;
		const BluetoothLEAdvertisement& ad = args.Advertisement();

		Windows::Foundation::IAsyncOperation<BluetoothLEDevice> deviceAsync = BluetoothLEDevice::FromBluetoothAddressAsync(args.BluetoothAddress());
		auto device = std::make_shared<BluetoothLEDevice>(co_await deviceAsync);
		std::cout << "Found device named " << to_string(device->Name()) << endl;
	
		gDevices[to_string(device->Name())] = DeviceSession::CreateSession(device);
	});
	
	gWatcher.Stopped([=](auto &&, auto &&)
	{
		cout << "Stopped";
	});

	gWatcher.Start();
}

void godice_stop_listening()
{
	gWatcher.Stop();
	gDevices.clear();
}
