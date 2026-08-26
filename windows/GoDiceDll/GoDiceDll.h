#pragma once

#include "stdafx.h"
#include "../../native/include/GoDiceTypes.h"

extern "C" {
	__declspec(dllexport) void godice_set_callbacks(
		GDDeviceFoundCallbackFunction deviceFoundCallback,
		GDDataCallbackFunction dataReceivedCallback,
		GDDeviceConnectedCallbackFunction deviceConnectedCallback,
		GDDeviceConnectionFailedCallbackFunction deviceConnectionFailedCallback,
		GDDeviceDisconnectedCallbackFunction deviceDisconnectedCallback,
		GDListenerStoppedCallbackFunction listenerStoppedCallback);
	__declspec(dllexport) void godice_set_device_state_callback(GDDeviceStateCallbackFunction deviceStateCallback);
	__declspec(dllexport) void godice_set_logger(GDLogger logger);
	__declspec(dllexport) void godice_start_listening();
	__declspec(dllexport) void godice_stop_listening();

	__declspec(dllexport) void godice_connect(const char* identifier);
	__declspec(dllexport) void godice_disconnect(const char* identifier);
	__declspec(dllexport) void godice_send(const char* identifier, uint32_t data_size, uint8_t* data);
	
	__declspec(dllexport) void godice_reset();
}
