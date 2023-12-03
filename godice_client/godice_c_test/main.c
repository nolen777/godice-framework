//
//  main.c
//  godice_c_test
//
//  Created by Dan Crosby on 12/2/23.
//

#include <stdio.h>
#include "UnityBridge.h"

void callback(const char* name, uint8_t x, uint8_t y, uint8_t z);

int main(int argc, const char * argv[]) {
    godice_set_roll_callback(callback);
    godice_start_listening();
    printf("Hello, World!\n");
    
    while(1);
    
    return 0;
}

void callback(const char* name, uint8_t x, uint8_t y, uint8_t z) {
    printf("Received (%d, %d, %d) for %s\n", x, y, z, name);
}
