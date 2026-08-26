//
//  Bridge.h
//
//  Provides C bridge functions for the Swift framework code.
//
//  Created by Dan Crosby on 12/2/23.
//

#ifndef GodiceFramework_Darwin_Framework_Bridge_h
#define GodiceFramework_Darwin_Framework_Bridge_h

#if __has_include(<GodiceClient/GoDiceTypes.h>)
#include <GodiceClient/GoDiceTypes.h>
#else
#include <GoDiceTypes.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void godice_set_callbacks(GDDeviceFoundCallbackFunction deviceFoundCallback,
                          GDDataCallbackFunction dataReceivedCallback,
                          GDDeviceConnectedCallbackFunction deviceConnectedCallback,
                          GDDeviceConnectionFailedCallbackFunction deviceConnectionFailedCallback,
                          GDDeviceDisconnectedCallbackFunction deviceDisconnectedCallback,
                          GDListenerStoppedCallbackFunction listenerStoppedCallback);
void godice_set_device_state_callback(GDDeviceStateCallbackFunction deviceStateCallback);
void godice_set_logger(GDLogger logger);
void godice_start_listening(void);
void godice_stop_listening(void);
void godice_connect(const char* identifier);
void godice_disconnect(const char* identifier);
void godice_send(const char* identifier, uint32_t data_size, uint8_t* data);
void godice_reset(void);
    
#ifdef __cplusplus
}
#endif

#endif /* GodiceFramework_Darwin_Framework_Bridge_h */
