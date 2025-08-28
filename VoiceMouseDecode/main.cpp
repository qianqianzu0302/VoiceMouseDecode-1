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

extern "C" void InitBLEAudio();
extern "C" void onAudioDataReceived(const uint8_t* data, size_t len);
extern "C" void onAIButtonEvent(uint8_t value);


extern std::ofstream pcmFile;
extern bool recording;

static struct timespec pressTime;
static bool aiKeyPressed = false;

PCMServer pcmServer;
static sbc_t sbc_context;
static bool sbc_initialized = false;


// self-defined AI key map
std::map<uint16_t, std::string> aiKeyMap = {
    {0x20, "AI 键"},
    {0xFF09, "AI 键 1 长文写作"},
    {0xFF10, "AI 键 2 智能体"},
    {0xFF11, "AI 键 3 PPT"},
    {0xFF06, "AI 键 4 方案策划"},
    {0xFF07, "AI 键 5 工作总结"},
    {0xFF08, "AI 键 6 演讲稿"},
    {0xFF03, "AI 键 7 文本润色"},
    {0xFF04, "AI 键 8 文稿校对"},
    {0xFF05, "AI 键 9 AI阅读"},
    {0xFF01, "AI 键 / 截图"},
    {0xFF02, "AI 键 * 绘图"},
    {0xFF12, "AI 键 0 图像识别"},
    {0xFF13, "AI 键 . 翻译"},
    {0xFF14, "AI 键 - 录音转写"},
    {0xFF15, "AI 键 + 写作"},
    {0xFF16, "AI 键 Enter AI问答"}
};

// device connect
void DeviceConnectedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    std::cout << "✅ HID device connected." << std::endl;
}

// device removal
void DeviceRemovedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    std::cout << "❌ HID device removed." << std::endl;
}

size_t sbc_decode(const uint8_t* input, size_t input_len, uint8_t* output, size_t output_max_len) {
    if (!sbc_initialized) {
        if (sbc_init(&sbc_context, 0) != 0) {
            std::cerr << "Failed to initialize SBC decoder\n";
            return 0;
        }
        sbc_initialized = true;
    }

    size_t written = 0;
    ssize_t decoded = ::sbc_decode(&sbc_context, input, input_len, output, output_max_len, &written);
    if (decoded < 0) {
        std::cerr << "sbc_decode failed\n";
        return 0;
    }

    return written;
}

void HandleInput(void* context, IOReturn result, void* sender, IOHIDValueRef value) {
    IOHIDElementRef element = IOHIDValueGetElement(value);
    uint32_t usagePage = IOHIDElementGetUsagePage(element);
    uint32_t usage = IOHIDElementGetUsage(element);
    CFIndex length = IOHIDValueGetLength(value);
    const uint8_t* data = (const uint8_t*)IOHIDValueGetBytePtr(value);
    
    std::cout << "usagePage = 0x" << std::hex << usagePage << ", usage = 0x" << std::hex << usage << std::endl;
    std::cout << "Input data (len=" << std::dec << length << "): ";
    for (CFIndex i = 0; i < length; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    std::cout << std::endl;
    
    // Handle audio data
    if (usagePage == 0xFF02 && length >= 3 && data[0] == 0x01)
    {
        if (!aiKeyPressed)
        {
            // Press AI key first time
            aiKeyPressed = true;
            clock_gettime(CLOCK_MONOTONIC, &pressTime);
            std::cout << "🔘 Press AI key" << std::endl;
        }
        else {
            struct timespec currentTime;
            clock_gettime(CLOCK_MONOTONIC, &currentTime);
            double duration = (currentTime.tv_sec - pressTime.tv_sec) + (currentTime.tv_nsec - pressTime.tv_nsec) / 1e9;
            //std::cout << "duration = " << duration << "s" << std::endl;
            if (duration > 0.5)
            {
                if (recording == false)
                {
                    std::cout << "⏱️ Long press AI key, start to receive audio..." << std::endl;
                    recording = true;
                    pcmServer.sendKeyboard(32, 1, 2);  // Send long press AI key event to client
                }
                
                // Handle audio decode
                static sbc_t sbc;
                static bool initialized = false;
                if (!initialized) {
                    sbc_init_msbc(&sbc, 0);
                    sbc.endian = SBC_LE;
                    initialized = true;
                }
                
                const size_t msbc_data_len = 57;
                const uint8_t* msbc_data = data + 2;
                
                int16_t pcm_output[240] = {0};
                size_t pcm_len = 0;
                
                ssize_t result = sbc_decode(&sbc, msbc_data, msbc_data_len, (uint8_t *)pcm_output, sizeof(pcm_output), &pcm_len);
                
                if (result > 0 && pcm_len > 0)
                {
                    if (!pcmFile.is_open())
                    {
                        pcmFile.open("audio_data_decoded.pcm", std::ios::binary | std::ios::trunc);
                        if (!pcmFile)
                        {
                            std::cerr << "❌ Can't open PCM file to write\n";
                            return;
                        }
                    }
                    
                    pcmFile.write(reinterpret_cast<const char*>(pcm_output), pcm_len);
                    pcmFile.flush();
                    // Can run "ffmpeg -f s16le -ar 16000 -ac 1 -i audio_data_decoded.pcm output.wav" to convert from pcm to wav
                    //std::cout << "✅ Write PCM: " << pcm_len << " bytes\n";
                    // Send audio data to client
                    pcmServer.sendAudioPCM((uint8_t*)pcm_output, pcm_len);
                }
                else
                {
                    std::cerr << "❌ mSBC decode failed, error code: " << result << std::endl;
                }
            }
        }
    }
    else if (usagePage == 0x0c && length == 1 && data[0] == 0x00)
    {
        // Release AI key
        if (aiKeyPressed)
        {
            struct timespec releaseTime;
            clock_gettime(CLOCK_MONOTONIC, &releaseTime);
            double duration = (releaseTime.tv_sec - pressTime.tv_sec) + (releaseTime.tv_nsec - pressTime.tv_nsec) / 1e9;
            //std::cout << "duration = " << duration << "s" << std::endl;
            if (duration >= 1)
            {
                std::cout << "🎤 audio data ends" << std::endl;
                recording = false;
                pcmServer.sendKeyboard(32, 0, 2);  // Send release AI key event to client
            }
            else
            {
                std::cout << "🖱️ Click AI key" << std::endl;
            }
            aiKeyPressed = false;
        }
    }
    // Handle keyboard press
    else if (usagePage == 0x0C && length == 2 && usage == 0xffffffff){
        uint16_t keyCode = data[1] << 8 | data[0];  // 小端
        auto it = aiKeyMap.find(keyCode);
        if (it != aiKeyMap.end())
        {
            std::cout << "Detect " << it->second << std::endl;
            //pcmServer.sendKeyboard(keyCode, 1, 0);
        }
        else if (keyCode == 0x0)
        {
            std::cout << "Release " << std::endl;
        }
        /*else
        {
            std::cout << "Unknown keyboard: 0x" << std::hex << keyCode << std::endl;
        }*/
    }
}

int ConnectWireless()
{
    // === start TCP server ===
    /*if (!pcmServer.start()) {
        std::cerr << "Failed to start PCM TCP server.\n";
        return -1;
    }
    std::cout << "Start TCP server " << std::endl;*/
    
    // === initialize HID Manager ===
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager) {
        std::cerr << "Failed to create IOHIDManager\n";
        return -1;
    }

    int vendorID = 0x248A;
    int productID = 0x8271;  // bluetooth PID

    // 创建匹配字典
    CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFNumberRef vendorIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendorID);
    CFNumberRef productIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID);

    CFDictionarySetValue(matchingDict, CFSTR(kIOHIDVendorIDKey), vendorIDRef);
    CFDictionarySetValue(matchingDict, CFSTR(kIOHIDProductIDKey), productIDRef);

    CFRelease(vendorIDRef);
    CFRelease(productIDRef);

    // 包装成数组
    CFMutableArrayRef matchingArray = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(matchingArray, matchingDict);
    CFRelease(matchingDict);

    // 传给 IOHIDManager
    IOHIDManagerSetDeviceMatchingMultiple(hidManager, matchingArray);
    CFRelease(matchingArray);
    
    // register device connect/remove callbacks
    IOHIDManagerRegisterDeviceMatchingCallback(hidManager, DeviceConnectedCallback, nullptr);
    IOHIDManagerRegisterDeviceRemovalCallback(hidManager, DeviceRemovedCallback, nullptr);

    IOHIDManagerRegisterInputValueCallback(hidManager, HandleInput, nullptr);
    IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        std::cerr << "Failed to open HID Manager\n";
        return -1;
    }
    
    //  print Transport type: USB or BLE
    CFSetRef deviceSet = IOHIDManagerCopyDevices(hidManager);
    if (deviceSet) {
        CFIndex count = CFSetGetCount(deviceSet);
        IOHIDDeviceRef* devices = (IOHIDDeviceRef*)malloc(sizeof(IOHIDDeviceRef) * count);
        CFSetGetValues(deviceSet, (const void**)devices);
        std::cout << count << " devices" <<std::endl;

        for (CFIndex i = 0; i < count; ++i) {
            IOHIDDeviceRef device = devices[i];

            // 打印 Transport
            CFTypeRef transport = IOHIDDeviceGetProperty(device, CFSTR("Transport"));
            char buffer[256] = {0};
            if (transport && CFGetTypeID(transport) == CFStringGetTypeID()) {
                CFStringGetCString((CFStringRef)transport, buffer, sizeof(buffer), kCFStringEncodingUTF8);
            }
            std::cout << "Device Transport: " << buffer << std::endl;

            // 打印 VendorID / ProductID
            int vid = 0, pid = 0;
            CFTypeRef vidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
            CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
            if (vidRef) CFNumberGetValue((CFNumberRef)vidRef, kCFNumberIntType, &vid);
            if (pidRef) CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);
            std::cout << "VendorID=0x" << std::hex << vid << " ProductID=0x" << pid << std::dec << std::endl;

            // 枚举 Elements
            CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, nullptr, kIOHIDOptionsTypeNone);
            if (elements) {
                CFIndex elemCount = CFArrayGetCount(elements);
                for (CFIndex j = 0; j < elemCount; ++j) {
                    IOHIDElementRef elem = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, j);
                    if (!elem) continue;

                    uint32_t usagePage = IOHIDElementGetUsagePage(elem);
                    uint32_t usage     = IOHIDElementGetUsage(elem);
                    uint32_t reportID  = IOHIDElementGetReportID(elem);

                    std::cout << "  Element: UsagePage=0x" << std::hex << usagePage
                              << " Usage=0x" << usage
                              << " ReportID=" << std::dec << reportID << std::endl;
                }
                CFRelease(elements);
            }
        }

        free(devices);
        CFRelease(deviceSet);
    }


    std::cout << "Listening for HID input and BLE audio...\n";
    
    // === initialize BLE ===
    //InitBLEAudio();  // 👈 调用 BLE 初始化
    CFRunLoopRun();

    // ==== Terminate and cleanup ===
    /*if (pcmFile.is_open()) {
        pcmFile.close();
    }
    
    pcmServer.stop(); // stop TCP server*/

    CFRelease(hidManager);
    
    return 0;
}

int main()
{
    int res;
        unsigned char buf[65]; // HID 报告最大长度，1字节report ID + 64数据字节

        // 初始化 HIDAPI
        if (hid_init()) {
            printf("Failed to initialize HIDAPI\n");
            return 1;
        }

        // 打开设备，替换为你的 VID/PID
       struct hid_device_info *devs, *cur_dev;
        const char *path_to_open = NULL;
        hid_device * handle = NULL;
        unsigned short vendor_id = 0x248a;
        unsigned short product_id = 0x8266;

        /* register_global_error: global error is reset by hid_enumerate/hid_init */
        devs = hid_enumerate(vendor_id, product_id);
        if (devs == NULL) {
            /* register_global_error: global error is already set by hid_enumerate */
            return NULL;
        }

        cur_dev = devs;
        while (cur_dev) {
            printf("Device Path: %s\n", cur_dev->path);
            printf("Usage Page: 0x%hx, Usage: 0x%hx\n\n",
                           cur_dev->usage_page, cur_dev->usage);
            if (cur_dev->vendor_id == vendor_id &&
                cur_dev->product_id == product_id &&
                cur_dev->usage_page == 0xff02)
            {
                path_to_open = cur_dev->path;
                break;
            }
            cur_dev = cur_dev->next;  // 一定要在 while 内部
        }


        if (path_to_open) {
            printf("will open ff02\n");
            handle = hid_open_path(path_to_open);
        } else {
            printf("Device with requested VID/PID/(SerialNumber) not found");
        }

        hid_free_enumeration(devs);


        // 读取数据
        while (1) {
            res = hid_read(handle, buf, sizeof(buf));
            printf("log\n");
            if (res > 0) {
                printf("Read %d bytes: ", res);
                for (int i = 0; i < res; i++)
                    printf("%02X ", buf[i]);
                printf("\n");
            }
            // 可加入延时，避免 CPU 占用过高
        }

        // 关闭设备
        hid_close(handle);
        hid_exit();

        return 0;
}
