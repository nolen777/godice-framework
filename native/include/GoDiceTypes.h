#ifndef GodiceFramework_Native_GoDiceTypes_h
#define GodiceFramework_Native_GoDiceTypes_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GDDeviceFoundCallbackFunction)(const char* identifier, const char* name);
typedef void (*GDDataCallbackFunction)(const char* identifier, uint32_t data_size, uint8_t* data);
typedef void (*GDDeviceConnectedCallbackFunction)(const char* identifier);
typedef void (*GDDeviceConnectionFailedCallbackFunction)(const char* identifier);
typedef void (*GDDeviceDisconnectedCallbackFunction)(const char* identifier);
typedef void (*GDListenerStoppedCallbackFunction)(void);
typedef void (*GDLogger)(const char* str);

typedef uint32_t GDConnectionState;
enum {
    GD_CONNECTION_STATE_UNKNOWN = 0,
    GD_CONNECTION_STATE_DISCOVERED = 1,
    GD_CONNECTION_STATE_CONNECTING = 2,
    GD_CONNECTION_STATE_SUBSCRIBING = 3,
    GD_CONNECTION_STATE_READY = 4,
    GD_CONNECTION_STATE_DISCONNECTING = 5,
    GD_CONNECTION_STATE_DISCONNECTED = 6,
    GD_CONNECTION_STATE_RETRY_WAIT = 7,
};

typedef uint32_t GDConnectionReason;
enum {
    GD_CONNECTION_REASON_NONE = 0,
    GD_CONNECTION_REASON_REQUESTED = 1,
    GD_CONNECTION_REASON_LINK_LOSS = 2,
    GD_CONNECTION_REASON_CONNECTION_FAILED = 3,
    GD_CONNECTION_REASON_PROTOCOL_ERROR = 4,
    GD_CONNECTION_REASON_ADAPTER_UNAVAILABLE = 5,
    GD_CONNECTION_REASON_RESET = 6,
};

typedef void (*GDDeviceStateCallbackFunction)(const char* identifier,
                                              GDConnectionState state,
                                              GDConnectionReason reason,
                                              int32_t native_status,
                                              uint64_t monotonic_milliseconds,
                                              const char* detail);

#ifdef __cplusplus
}
#endif

#endif /* GodiceFramework_Native_GoDiceTypes_h */
