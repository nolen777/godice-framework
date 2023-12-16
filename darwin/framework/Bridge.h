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

void godice_set_callbacks(GDDeviceFoundCallbackFunction deviceFoundCallback,
                          GDDataCallbackFunction dataReceivedCallback);
void godice_start_listening(void);
void godice_stop_listening(void);
void godice_connect(const char* identifier);
    
#ifdef __cplusplus
}
#endif

#endif /* GodiceFramework_Darwin_Framework_Bridge_h */
