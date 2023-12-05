//
//  UnityBridge.c
//  godice_client_lib
//
//  Created by Dan Crosby on 12/2/23.
//

#include "UnityBridge.h"

extern void set_data_callback(void (^)(const char*, uint32_t data_size, uint8_t* data));
extern void start_listening(void);
extern void stop_listening(void);

void godice_set_callback(GDVectorCallbackFunction callback) {
    set_data_callback(^(const char* name, uint32_t data_size, uint8_t* data) {
        callback(name, data_size, data);
    });
}

void godice_start_listening(void) {
    start_listening();
}
void godice_stop_listening(void) {
    stop_listening();
}

