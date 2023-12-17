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
extern void set_device_connected_callback(void (^)(const char* identifier));
extern void set_device_disconnected_callback(void (^)(const char* identifier));
extern void set_listener_stopped_callback(void (^)(void));
extern void start_listening(void);
extern void stop_listening(void);
extern void connect_device(const char* identifier);
extern void disconnect_device(const char* identifier);
extern void send_data(const char* identifier, uint32_t data_size, uint8_t* data);

void godice_set_callbacks(GDDeviceFoundCallbackFunction deviceFoundCallback,
                          GDDataCallbackFunction dataReceivedCallback,
                          GDDeviceConnectedCallbackFunction deviceConnectedCallback,
                          GDDeviceDisconnectedCallbackFunction deviceDisconnectedCallback,
                          GDListenerStoppedCallbackFunction listenerStoppedCallback) {
    set_device_found_callback(^(const char* identifier, const char* name) {
        deviceFoundCallback(identifier, name);
    });
    
    set_data_callback(^(const char* identifier, uint32_t data_size, uint8_t* data) {
        dataReceivedCallback(identifier, data_size, data);
    });
    
    set_device_connected_callback(^(const char* identifier) {
        deviceConnectedCallback(identifier);
    });
    
    set_device_disconnected_callback(^(const char* identifier) {
        deviceDisconnectedCallback(identifier);
    });
    
    set_listener_stopped_callback(^(void) {
        listenerStoppedCallback();
    });
}

void godice_start_listening(void) {
    start_listening();
}

void godice_stop_listening(void) {
    stop_listening();
}

void godice_connect(const char* identifier) {
    connect_device(identifier);
}

void godice_disconnect(const char* identifier) {
    disconnect_device(identifier);
}

void godice_send(const char* identifier, uint32_t data_size, uint8_t* data) {
    send_data(identifier, data_size, data);
}
