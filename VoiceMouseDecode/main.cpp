#include <iostream>
#include <IOKit/hid/IOHIDLib.h>
#include <CoreFoundation/CoreFoundation.h>

void PrintElements(IOHIDDeviceRef device) {
    // 获取所有元素
    CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, nullptr, kIOHIDOptionsTypeNone);
    if (!elements) {
        std::cout << "No elements found." << std::endl;
        return;
    }

    CFIndex count = CFArrayGetCount(elements);
    std::cout << "Element count: " << count << std::endl;

    for (CFIndex i = 0; i < count; ++i) {
        IOHIDElementRef elem = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
        if (!elem) continue;

        uint32_t usagePage = IOHIDElementGetUsagePage(elem);
        uint32_t usage     = IOHIDElementGetUsage(elem);
        uint32_t reportID  = IOHIDElementGetReportID(elem);
        CFIndex reportSize = IOHIDElementGetReportSize(elem);
        CFIndex reportCount = IOHIDElementGetReportCount(elem);

        std::cout << "Element "
                  << "UsagePage=0x" << std::hex << usagePage
                  << " Usage=0x" << usage
                  << " ReportID=0x" << reportID
                  << " Size=" << std::dec << reportSize
                  << " Count=" << reportCount
                  << std::endl;
    }

    CFRelease(elements);
}

int main() {
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    int vendorID  = 0x248A;
    int productID = 0x8266;  // BLE PID

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

    IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);

    CFSetRef deviceSet = IOHIDManagerCopyDevices(hidManager);
    if (!deviceSet) {
        std::cerr << "No devices found" << std::endl;
        return -1;
    }

    CFIndex count = CFSetGetCount(deviceSet);
    IOHIDDeviceRef* devices = (IOHIDDeviceRef*)malloc(sizeof(IOHIDDeviceRef) * count);
    CFSetGetValues(deviceSet, (const void**)devices);

    for (CFIndex i = 0; i < count; ++i) {
        std::cout << "=== Device " << i << " ===" << std::endl;
        PrintElements(devices[i]);
    }

    free(devices);
    CFRelease(deviceSet);
    CFRelease(hidManager);
    return 0;
}
