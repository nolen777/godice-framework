//
//  main.c
//  godice_c_test
//
//  Created by Dan Crosby on 12/2/23.
//

#include <stdio.h>

//extern void set_up(void);
extern void start_listening(void);

int main(int argc, const char * argv[]) {
   // set_up();
    start_listening();
    printf("Hello, World!\n");
    
    while(1);
    
    return 0;
}
