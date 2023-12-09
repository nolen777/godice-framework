//
//  main.c
//  godice_c_test
//
//  Created by Dan Crosby on 12/2/23.
//

#include <stdio.h>
#include "../framework/Bridge.h"

void callback(const char* name, uint32_t data_size, uint8_t* data);

int main(int argc, const char * argv[]) {
    godice_set_callback(callback);
    godice_start_listening();
    printf("Hello, World!\n");
    
    while(1);
    
    return 0;
}

void callback(const char* name, uint32_t data_size, uint8_t* data) {
    printf("Received %d bytes for %s\n", data_size, name);
}
