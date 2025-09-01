#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#include <iostream>
#include <iomanip>
 
// 打印单个 HID Element 的信息
void PrintElementInfo(IOHIDElementRef elem) {
    uint32_t usagePage = IOHIDElementGetUsagePage(elem);
    uint32_t usage     = IOHIDElementGetUsage(elem);
    uint32_t reportID  = IOHIDElementGetReportID(elem);
 
    std::cout << "    Element -> ReportID=0x" << std::hex << reportID
<< " UsagePage=0x" << usagePage
<< " Usage=0x" << usage << std::dec << std::endl;
}
 
// 递归打印所有元素
void PrintAllElements(IOHIDDeviceRef device) {
    CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, nullptr, kIOHIDOptionsTypeNone);
    if (!elements) return;
 
    CFIndex count = CFArrayGetCount(elements);
    for (CFIndex i = 0; i < count; ++i) {
        IOHIDElementRef elem = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
        if (elem) {
            PrintElementInfo(elem);
        }
    }
    CFRelease(elements);
}
 
// 设备连接时的回调
void DeviceConnectedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    CFTypeRef vidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
    CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
    int vid = 0, pid = 0;
 
    if (vidRef && CFGetTypeID(vidRef) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)vidRef, kCFNumberIntType, &vid);
    if (pidRef && CFGetTypeID(pidRef) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);
 
    std::cout << "✅ Device connected: VID=0x" << std::hex << vid
<< " PID=0x" << pid << std::dec << std::endl;
 
    std::cout << "  Elements:" << std::endl;
    PrintAllElements(device);
}
 
// 设备移除时的回调
void DeviceRemovedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    std::cout << "❌ Device removed" << std::endl;
}
 
// 输入数据回调
void HandleInput(void* context, IOReturn result, void* sender, IOHIDValueRef value) {
    IOHIDElementRef elem = IOHIDValueGetElement(value);
 
    uint32_t usagePage = IOHIDElementGetUsagePage(elem);
    uint32_t usage     = IOHIDElementGetUsage(elem);
    uint32_t reportID  = IOHIDElementGetReportID(elem);
 
    CFIndex length = IOHIDValueGetLength(value);
    const uint8_t* data = (const uint8_t*)IOHIDValueGetBytePtr(value);
 
    std::cout << "📥 Input report:"
<< " ReportID=0x" << std::hex << reportID
<< " UsagePage=0x" << usagePage
<< " Usage=0x" << usage
<< " Length=" << std::dec << length
<< std::endl;
 
    if (data && length > 0) {
        std::cout << "    Data: ";
        for (CFIndex i = 0; i < length; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
<< (int)data[i] << " ";
        }
        std::cout << std::dec << std::endl;
    }
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
 
    IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
 
    IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        std::cerr << "Failed to open IOHIDManager: " << ret << std::endl;
        return -1;
    }
 
    std::cout << "🚀 Listening for all HID devices..." << std::endl;
    CFRunLoopRun();
    return 0;
}
