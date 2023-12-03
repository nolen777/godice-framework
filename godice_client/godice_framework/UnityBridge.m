//
//  UnityBridge.m
//  godice_client_lib
//
//  Created by Dan Crosby on 12/2/23.
//

#import <Foundation/Foundation.h>
#import "UnityBridge.h"
#import "godice_client_lib-Swift.h"

void godice_set_roll_callback(GDVectorCallbackFunction callback) {
    GoDiceBLEController* controller = get_controller();
    
    [controller setDiceVectorCallbackWithCb: ^(NSString* name, uint8_t x, uint8_t y, uint8_t z) {
        callback([name UTF8String], x, y, z);
    }];
}

void godice_start_listening() {
    [get_controller() startListening];
}
void godice_stop_listening() {
    [get_controller() stopListening];
}

