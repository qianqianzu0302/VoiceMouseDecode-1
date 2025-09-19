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

        /*std::cout << "🔹 Device: " << [name UTF8String]
                  << "  MAC=" << [addr UTF8String]
                  << "  Major=" << dev.deviceClassMajor
                  << "  Minor=" << dev.deviceClassMinor
                  << std::endl;*/

        // 匹配鼠标
        if ([name rangeOfString:@"AI" options:NSCaseInsensitiveSearch].location != NSNotFound) {
            return std::string([addr UTF8String]);
        }
    }
    return ""; // 没找到返回空
}
