#pragma once

#include "stdafx.h"

extern "C" {
	typedef void (*GDVectorCallbackFunction)(const char* name, uint32_t data_size, uint8_t* data);

	__declspec(dllexport) void godice_set_callback(GDVectorCallbackFunction callback);
	__declspec(dllexport) void godice_start_listening();
	__declspec(dllexport) void godice_stop_listening(void);
}
