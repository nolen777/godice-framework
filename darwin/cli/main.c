//
//  main.c
//  godice_c_test
//
//  Created by Dan Crosby on 12/2/23.
//

#include <stdio.h>
#include "../framework/Bridge.h"

void deviceFoundCallback(const char* identifier, const char* name);
void dataReceivedCallback(const char* identifier, uint32_t data_size, uint8_t* data);
void deviceConnectedCallback(const char* identifier);
void deviceDisconnectedCallback(const char* identifier);
void listenerStoppedCallback(void);

int main(int argc, const char * argv[]) {
    godice_set_callbacks(deviceFoundCallback,
                         dataReceivedCallback,
                         deviceConnectedCallback, 
                         deviceDisconnectedCallback,
                         listenerStoppedCallback);
    godice_start_listening();
    printf("Hello, World!\n");
    
    while(1);
    
    return 0;
}

void deviceFoundCallback(const char* identifier, const char* name) {
    printf("Found device %s: %s\n", identifier, name);
    godice_connect(identifier);
}

void dataReceivedCallback(const char* identifier, uint32_t data_size, uint8_t* data) {
    printf("Received %d bytes for %s\n", data_size, identifier);
    
    if (data_size > 1) {
        printf("Flashing!\n");
        
        uint8_t pulseMessage[] = { 
            (uint8_t) 16 /* set LED toggle */,
            0x05 /* pulse count */,
            0x04 /* onTime */,
            0x04 /* offTime */,
            0xFF /* red */,
            0x00 /* green */,
            0x00 /* blue */,
            0x01,
            0x00};
        godice_send(identifier, sizeof(pulseMessage), pulseMessage);
    }
}

void deviceConnectedCallback(const char* identifier) {
    printf("Device %s connected!\n", identifier);
    
    uint8_t msg[] = { 23 };
    godice_send(identifier, 1, msg);
}

void deviceDisconnectedCallback(const char* identifier) {
    printf("Device %s disconnected!\n", identifier);
}

void listenerStoppedCallback(void) {
    printf("Listener stopped!\n");
}
