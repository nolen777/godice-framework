//
//  Bridge.c
//
//  Provides C bridge functions for the Swift framework code.
//
//  Created by Dan Crosby on 12/2/23.
//

#include "Bridge.h"

extern void set_device_found_callback(void (^)(const char* identifier, const char* name));
extern void set_data_callback(void (^)(const char* identifier, uint32_t data_size, uint8_t* data));
extern void start_listening(void);
extern void stop_listening(void);
extern void connect_device(uint64_t);

void godice_set_callbacks(GDDeviceFoundCallbackFunction deviceFoundCallback,
                          GDDataCallbackFunction dataReceivedCallback) {
    set_device_found_callback(^(const char* identifier, const char* name) {
        deviceFoundCallback(identifier, name);
    });
    
    set_data_callback(^(const char* identifier, uint32_t data_size, uint8_t* data) {
        dataReceivedCallback(identifier, data_size, data);
    });
}

void godice_start_listening(void) {
    start_listening();
}
void godice_stop_listening(void) {
    stop_listening();
}

void godice_connect(uint64_t addr) {
    
}
