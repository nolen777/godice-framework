//
//  Bridge.h
//
//  Provides C bridge functions for the Swift framework code.
//
//  Created by Dan Crosby on 12/2/23.
//

#ifndef GodiceFramework_Darwin_Framework_Bridge_h
#define GodiceFramework_Darwin_Framework_Bridge_h

#include <stdint.h>

#ifdef __cplusplus
extern “C” {
#endif

typedef void (*GDDeviceFoundCallbackFunction)(const char* identifier, const char* name);
typedef void (*GDDataCallbackFunction)(const char* identifier, uint32_t data_size, uint8_t* data);
typedef void (*GDDeviceConnectedCallbackFunction)(const char* identifier);
typedef void (*GDDeviceDisconnectedCallbackFunction)(const char* identifier);
typedef void (*GDListenerStoppedCallbackFunction)(void);
typedef void (*GDLogger)(const char* str);

void godice_set_callbacks(GDDeviceFoundCallbackFunction deviceFoundCallback,
                          GDDataCallbackFunction dataReceivedCallback,
                          GDDeviceConnectedCallbackFunction deviceConnectedCallback,
                          GDDeviceDisconnectedCallbackFunction deviceDisconnectedCallback,
                          GDListenerStoppedCallbackFunction listenerStoppedCallback);
void godice_set_logger(GDLogger logger);
void godice_start_listening(void);
void godice_stop_listening(void);
void godice_connect(const char* identifier);
void godice_disconnect(const char* identifier);
void godice_send(const char* identifier, uint32_t data_size, uint8_t* data);
    
#ifdef __cplusplus
}
#endif

#endif /* GodiceFramework_Darwin_Framework_Bridge_h */
