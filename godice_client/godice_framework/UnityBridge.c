//
//  UnityBridge.c
//  godice_client_lib
//
//  Created by Dan Crosby on 12/2/23.
//

//#import <Foundation/Foundation.h>
#include "UnityBridge.h"
//#import "godice_client_lib-Swift.h"

extern void set_dice_vector_callback(void (^)(const char*, uint8_t, uint8_t, uint8_t));
extern void start_listening();
extern void stop_listening();

void godice_set_roll_callback(GDVectorCallbackFunction callback) {
    set_dice_vector_callback(^(const char* name, uint8_t x, uint8_t y, uint8_t z) {
        callback(name, x, y, z);
    });
}

void godice_start_listening() {
    start_listening();
}
void godice_stop_listening() {
    stop_listening();
}

