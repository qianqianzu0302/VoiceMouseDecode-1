#include <iostream>
#include <IOKit/hid/IOHIDLib.h>
#include <CoreFoundation/CoreFoundation.h>

int main() {
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager) {
        std::cerr << "Failed to create IOHIDManager\n";
        return -1;
    }

    // 只匹配你的蓝牙鼠标
    int vendorID  = 0x248A;
    int productID = 0x8266; // BLE PID

    CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    CFNumberRef vendorIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendorID);
    CFNumberRef productIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID);

    CFDictionarySetValue(matchingDict, CFSTR(kIOHIDVendorIDKey), vendorIDRef);
    CFDictionarySetValue(matchingDict, CFSTR(kIOHIDProductIDKey), productIDRef);

    IOHIDManagerSetDeviceMatching(hidManager, matchingDict);

    CFRelease(vendorIDRef);
    CFRelease(productIDRef);
    CFRelease(matchingDict);

    // 打开 Manager
    IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        std::cerr << "Failed to open HID Manager\n";
        return -1;
    }

    // 获取设备集合
    CFSetRef deviceSet = IOHIDManagerCopyDevices(hidManager);
    if (!deviceSet) {
        std::cerr << "No devices found\n";
        return -1;
    }

    CFIndex count = CFSetGetCount(deviceSet);
    IOHIDDeviceRef* devices = (IOHIDDeviceRef*)malloc(sizeof(IOHIDDeviceRef) * count);
    CFSetGetValues(deviceSet, (const void**)devices);

    for (CFIndex i = 0; i < count; ++i) {
        IOHIDDeviceRef device = devices[i];

        // 打印设备信息
        int vid = 0, pid = 0;
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)), kCFNumberIntType, &vid);
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)), kCFNumberIntType, &pid);
        std::cout << "Device VID=0x" << std::hex << vid
                  << " PID=0x" << pid << std::dec << std::endl;

        // 获取 Report Descriptor
        CFDataRef desc = (CFDataRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDReportDescriptorKey));
        if (desc) {
            CFIndex len = CFDataGetLength(desc);
            const UInt8* bytes = CFDataGetBytePtr(desc);
            std::cout << "Report Descriptor (" << len << " bytes):" << std::endl;
            for (CFIndex j = 0; j < len; j++) {
                printf("%02X ", bytes[j]);
                if ((j + 1) % 16 == 0) printf("\n");
            }
            printf("\n");
        } else {
            std::cout << "No Report Descriptor available" << std::endl;
        }
    }

    free(devices);
    CFRelease(deviceSet);
    CFRelease(hidManager);

    return 0;
}
