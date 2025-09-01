
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "hidapi.h"

#define PACKETSIZE  100

uint8_t inputBuffer[PACKETSIZE];
uint8_t outputBuffer[PACKETSIZE];

struct hid_device_info *device_info;


/* Main function */

int main(int argc, char **argv) {

    /* Check at least 2 argument */
    
    uint16_t vid = 0x248a;
    
    uint16_t pid = 0xca10;
    
    /* Connect to device */
    printf("will connect to device\n");
    device_info = hid_enumerate(vid, pid);
    
    
    if (device_info != NULL) {
        
        hid_device *device = hid_open(device_info->vendor_id, device_info->product_id, device_info->serial_number);
        
        if (device != NULL) {
            
            /*int i = 3;
            
            while (i < argc) {
                
                int j = 0;
                
                memset(outputBuffer, 0, PACKETSIZE);
                
                while (j < PACKETSIZE && i < argc) {
                    
                    outputBuffer[j] = strtol(argv[i], NULL, 0);
                    
                    i += 1;
                    j += 1;
                    
                }
                
                hid_write(device, outputBuffer, PACKETSIZE);*/
            printf("open device\n");
            while(1)
            {
                
                int res = hid_read_timeout(device, inputBuffer, 100, -1);
                if (res > 0) {
                            printf("Read %d bytes: \n", res);
                            for (int i = 0; i < res; i++)
                                printf("%02X ", inputBuffer[i]);
                            printf("\n");
                        }
                /*if (length != PACKETSIZE) {
                    
                    printf("ERROR: Incorrect response from device\n");
                    
                    return 0;
                    
                }*/
                
            }
            
            hid_close(device);
            
        } else {
            
            printf("ERROR: Could not connect to device\n");
            
        }
            
    } else {
        
        printf("NULL\n");
        
    }

    return 0;

}
