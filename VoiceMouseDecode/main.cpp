#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <iostream>
#include <iomanip>

void DeviceConnectedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    CFTypeRef vidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
    CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
    CFTypeRef usagePageRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDPrimaryUsagePageKey));
    CFTypeRef usageRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDPrimaryUsageKey));

    int vid = 0, pid = 0, usagePage = 0, usage = 0;
    if (vidRef && CFGetTypeID(vidRef) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)vidRef, kCFNumberIntType, &vid);
    if (pidRef && CFGetTypeID(pidRef) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);
    if (usagePageRef && CFGetTypeID(usagePageRef) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)usagePageRef, kCFNumberIntType, &usagePage);
    if (usageRef && CFGetTypeID(usageRef) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)usageRef, kCFNumberIntType, &usage);

    std::cout << "✅ Device connected: VID=0x" << std::hex << vid
              << " PID=0x" << pid
              << " UsagePage=0x" << usagePage
              << " Usage=0x" << usage << std::dec << std::endl;
}


void DeviceRemovedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    std::cout << "❌ Device removed" << std::endl;
}

void HandleInput(void* context, IOReturn result, void* sender, IOHIDValueRef value) {
    IOHIDElementRef elem = IOHIDValueGetElement(value);
    CFIndex intValue = IOHIDValueGetIntegerValue(value);
    uint32_t usagePage = IOHIDElementGetUsagePage(elem);
    CFIndex length = IOHIDValueGetLength(value);
    uint32_t usage = IOHIDElementGetUsage(elem);
    const uint8_t* data = (const uint8_t*)IOHIDValueGetBytePtr(value);
    
    std::cout << "usagePage = 0x" << std::hex << usagePage << ", usage = 0x" << std::hex << usage << std::endl;
    std::cout << "Input data (len=" << std::dec << length << "): ";
    for (CFIndex i = 0; i < length; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    std::cout << std::endl << std::endl;
}
    
int main() {
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager) {
        std::cerr << "Failed to create IOHIDManager" << std::endl;
        return -1;
    }
    
    // 匹配所有 HID 设备
    IOHIDManagerSetDeviceMatching(hidManager, nullptr);
    
    // 注册回调
    IOHIDManagerRegisterDeviceMatchingCallback(hidManager, DeviceConnectedCallback, nullptr);
    IOHIDManagerRegisterDeviceRemovalCallback(hidManager, DeviceRemovedCallback, nullptr);
    IOHIDManagerRegisterInputValueCallback(hidManager, HandleInput, nullptr);
    
    // 将管理器加入当前 runloop
    IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    
    IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        std::cerr << "Failed to open IOHIDManager" << std::endl;
        CFRelease(hidManager);
        return -1;
    }
    
    std::cout << "🚀 Listening for all HID devices..." << std::endl;
    CFRunLoopRun();  // 阻塞运行，直到手动退出
    
    CFRelease(hidManager);
    return 0;
}
