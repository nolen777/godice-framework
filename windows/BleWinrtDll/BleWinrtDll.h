#pragma once

#include "stdafx.h"

extern "C" {
	typedef void (*GDDeviceFoundCallbackFunction)(const char*, const char* name);
	typedef void (*GDDataCallbackFunction)(const char*, uint32_t data_size, uint8_t* data);
	typedef void (*GDDeviceConnectedCallbackFunction)(const char* identifier);
	typedef void (*GDDeviceDisconnectedCallbackFunction)(const char* identifier);
	typedef void (*GDListenerStoppedCallbackFunction)(void);

	__declspec(dllexport) void godice_set_callbacks(
		GDDeviceFoundCallbackFunction deviceFoundCallback,
		GDDataCallbackFunction dataReceivedCallback,
		GDDeviceConnectedCallbackFunction deviceConnectedCallback,
		GDDeviceDisconnectedCallbackFunction deviceDisconnectedCallback,
		GDListenerStoppedCallbackFunction listenerStoppedCallback);
	__declspec(dllexport) void godice_start_listening();
	__declspec(dllexport) void godice_stop_listening();

	__declspec(dllexport) void godice_connect(const char* identifier);
	__declspec(dllexport) void godice_disconnect(const char* identifier);
	__declspec(dllexport) void godice_send(const char* identifier, uint32_t data_size, uint8_t* data);
}
