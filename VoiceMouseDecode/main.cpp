#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <cstring>
#include "sbc.h"
#include "PCMServer.h"
#include <time.h>
#include "denoise.h"
#include "hidapi.h"

// print report
void MyReportCallback(
    void* context,
    IOReturn result,
    void* sender,
    IOHIDReportType type,
    uint32_t reportID,
    uint8_t* report,
    CFIndex reportLength)
{
    std::cout << "Got report (ID=" << reportID
              << ", Len=" << reportLength << "): ";
    for (CFIndex i = 0; i < reportLength; i++) {
        printf("%02X ", report[i]);
    }
    printf("\n");
}


int main()
{
    
    // === initialize HID Manager ===
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
        if (!hidManager) {
            std::cerr << "Failed to create IOHIDManager\n";
            return -1;
        }

        int vendorID = 0x248A;
        int productID = 0x8266;  // 蓝牙 PID

        // 创建匹配字典
        CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

        CFNumberRef vendorIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendorID);
        CFNumberRef productIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID);

        CFDictionarySetValue(matchingDict, CFSTR(kIOHIDVendorIDKey), vendorIDRef);
        CFDictionarySetValue(matchingDict, CFSTR(kIOHIDProductIDKey), productIDRef);

        CFRelease(vendorIDRef);
        CFRelease(productIDRef);

        CFMutableArrayRef matchingArray = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        CFArrayAppendValue(matchingArray, matchingDict);
        CFRelease(matchingDict);

        IOHIDManagerSetDeviceMatchingMultiple(hidManager, matchingArray);
        CFRelease(matchingArray);

        // 设备 connect/remove 回调（可选）
        IOHIDManagerRegisterDeviceMatchingCallback(hidManager,
            [](void* ctx, IOReturn, void*, IOHIDDeviceRef device) {
                std::cout << "Device connected" << std::endl;

                // 获取最大 report size
                CFTypeRef prop = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDMaxInputReportSizeKey));
                CFIndex maxReportSize = 64; // fallback
                if (prop) {
                    CFNumberGetValue((CFNumberRef)prop, kCFNumberCFIndexType, &maxReportSize);
                }
                std::cout << "Max Report Size = " << maxReportSize << std::endl;

                // 分配 buffer 并注册 InputReport 回调
                uint8_t* reportBuffer = (uint8_t*)malloc(maxReportSize);
                IOHIDDeviceRegisterInputReportCallback(
                    device,
                    reportBuffer,
                    maxReportSize,
                    MyReportCallback,
                    nullptr
                );
            }, nullptr);

        IOHIDManagerRegisterDeviceRemovalCallback(hidManager,
            [](void* ctx, IOReturn, void*, IOHIDDeviceRef) {
                std::cout << "Device removed" << std::endl;
            }, nullptr);

        // 把 Manager 加到 RunLoop
        IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

        IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
        if (ret != kIOReturnSuccess) {
            std::cerr << "Failed to open HID Manager\n";
            return -1;
        }

        std::cout << "Listening for HID input reports (BLE audio)...\n";
        CFRunLoopRun();

        CFRelease(hidManager);
        return 0;
}
