#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


#include <iostream>
#include <queue>
#include <map>
#include <mutex>
#include <condition_variable>

#include <winrt/Windows.Foundation.Collections.h>

#include "winrt/Windows.Devices.Bluetooth.h"
#include "winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h"
#include "winrt/Windows.Devices.Bluetooth.Advertisement.h"

#include "winrt/Windows.Storage.Streams.h"
