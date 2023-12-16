#pragma once

#include "stdafx.h"

extern "C" {
	typedef void (*GDDeviceFoundCallbackFunction)(const char*, const char* name);
	typedef void (*GDDataCallbackFunction)(const char*, uint32_t data_size, uint8_t* data);

	__declspec(dllexport) void godice_set_callbacks(
		GDDeviceFoundCallbackFunction deviceFoundCallback,
		GDDataCallbackFunction dataReceivedCallback);
	__declspec(dllexport) void godice_start_listening();
	__declspec(dllexport) void godice_stop_listening();

	__declspec(dllexport) void godice_connect(const char* identifier);
}
