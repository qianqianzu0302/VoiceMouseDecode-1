#include <iostream>
#import <IOBluetooth/IOBluetooth.h>

std::string getBluetoothMouseMac() {
    NSArray *devices = [IOBluetoothDevice pairedDevices];
    if (!devices) return "";

    for (IOBluetoothDevice *dev in devices) {
        if (!dev || !dev.isConnected) continue;

        NSString *name = dev.name;
        NSString *addr = dev.addressString;
        if (!name || !addr) continue; // 避免 nil 崩溃
        
        /*NSLog(@"[BLE] Device: name=%@ | addr=%@| class=0x%X",
                      name,
                      addr,
                      dev.deviceClassMajor);*/

        // 匹配鼠标
        if ([name rangeOfString:@"AI" options:NSCaseInsensitiveSearch].location != NSNotFound) {
            // 将 "-" 替换为 ":"
            NSString *colonAddr = [addr stringByReplacingOccurrencesOfString:@"-" withString:@":"];
            return std::string([colonAddr UTF8String]);
        }
    }
    return ""; // 没找到返回空
}







