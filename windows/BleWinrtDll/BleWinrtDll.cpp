// BleWinrtDll.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
//

#include "stdafx.h"

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
	static std::shared_ptr<DeviceSession> CreateSession(const std::shared_ptr<BluetoothLEDevice>& dev)
	{
		const auto services = dev->GetGattServicesAsync().get().Services();

		for (const GattDeviceService& svc : services)
		{
			if (svc.Uuid() != kServiceGuid) continue;

			// FIXME: make this async
			GattCharacteristic wCh = svc.GetCharacteristicsForUuidAsync(kWriteGuid).get().Characteristics().GetAt(0);
			GattCharacteristic nCh = svc.GetCharacteristicsForUuidAsync(kNotifyGuid).get().Characteristics().GetAt(0);

			GattCommunicationStatus status = nCh.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

			if (status == GattCommunicationStatus::Success)
			{
				return std::make_shared<DeviceSession>(dev, wCh, nCh);
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

	gWatcher.Received([=](auto && watcher, auto && args) {
		cout << "Received" << std::endl;
		const BluetoothLEAdvertisement& ad = args.Advertisement();

		// FIXME: this should be async
		auto device = std::make_shared<BluetoothLEDevice>(BluetoothLEDevice::FromBluetoothAddressAsync(args.BluetoothAddress()).get());
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
