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

int main(int argc, const char * argv[]) {
    godice_set_callbacks(deviceFoundCallback, dataReceivedCallback);
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
}
