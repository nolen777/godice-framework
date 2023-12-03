//
//  UnityBridge.h
//  godice_client_lib
//
//  Created by Dan Crosby on 12/2/23.
//

#ifndef UnityBridge_h
#define UnityBridge_h

#ifdef __cplusplus
extern “C” {
#endif

typedef void (*GDVectorCallbackFunction)(const char* name, uint8_t x, uint8_t y, uint8_t z);

void godice_set_roll_callback(GDVectorCallbackFunction callback);
void godice_start_listening();
void godice_stop_listening();
    
#ifdef __cplusplus
}
#endif

#endif /* UnityBridge_h */
