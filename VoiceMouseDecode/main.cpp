#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOBluetooth/IOBluetooth.h>
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
#include <regex>


std::map<IOHIDDeviceRef, uint32_t> deviceUsagePage; // 保存设备和usagePage映射

std::ofstream pcmFile;
bool recording;

static struct timespec pressTime;
static bool aiKeyPressed = false;

PCMServer pcmServer;
static sbc_t sbc_context;
static bool sbc_initialized = false;

uint32_t audioUsagePage;

std::string getBluetoothMouseMac();

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

std::map<IOHIDDeviceRef, std::string> deviceMacMap;
// device connect
void DeviceConnectedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
    int pid = 0;
    if (pidRef) CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);

    if (pid == 0x8266) {
        std::cout << "✅ Bluetooth mouse connected" << std::endl;
        deviceUsagePage[device] = 0xFF12;
        /*std::string mac = getBluetoothMouseMac();
        if (!mac.empty()) {
            deviceMacMap[device] = mac;   // 只在有值时插入
            pcmServer.sendDeviceConnect(mac, 4, 1, mac);
        } else {
            std::cout << "⚠️ Could not find MAC for Bluetooth mouse" << std::endl;
        }*/
    }
    else if (pid == 0xCA10) {
        std::cout << "✅ 2.4G device connected" << std::endl;
        deviceUsagePage[device] = 0xFF02;
    }
    else if (pid == 0x8208) {
        std::cout << "✅ Bluetooth keyboard connected" << std::endl;
        // 键盘不处理音频，不放入 map
    }
    
}


// device removal
void DeviceRemovedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
    int pid = 0;
    if (pidRef) CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);

    if (pid == 0x8266) {
        std::cout << "✅ Bluetooth mouse disconnected" << std::endl;
        auto it = deviceMacMap.find(device);
        if (it != deviceMacMap.end()) {
            pcmServer.sendDeviceDisconnect(it->second, 4, 1);
            deviceMacMap.erase(it);
        } else {
            std::cout << "⚠️ MAC not found for disconnected device" << std::endl;
        }
    }
    else if (pid == 0xCA10) {
        std::cout << "✅ 2.4G device disconnected" << std::endl;
    }
    else if (pid == 0x8208) {
        std::cout << "✅ Bluetooth keyboard disconnected" << std::endl;
        // 键盘不处理音频，不放入 map
    }

    deviceUsagePage.erase(device); // 移除映射
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
    IOHIDDeviceRef dev = IOHIDElementGetDevice(element);
    
    uint32_t usagePage = IOHIDElementGetUsagePage(element);
    uint32_t usage = IOHIDElementGetUsage(element);
    CFIndex length = IOHIDValueGetLength(value);
    const uint8_t* data = (const uint8_t*)IOHIDValueGetBytePtr(value);
    
    /*std::cout << "usagePage = 0x" << std::hex << usagePage << ", usage = 0x" << std::hex << usage << std::endl;
     std::cout << "Input data (len=" << std::dec << length << "): ";
     for (CFIndex i = 0; i < length; ++i) {
     std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
     }
     std::cout << std::endl;*/
    
    // Handle audio data
    auto it = deviceUsagePage.find(dev);
    if (it != deviceUsagePage.end()) {
        uint32_t expectedUsagePage = it->second;
        
        if (usagePage == expectedUsagePage && length >= 3 && data[0] == 0x01)
        {
            if (!aiKeyPressed)
            {
                // Press AI key first time
                aiKeyPressed = true;
                clock_gettime(CLOCK_MONOTONIC, &pressTime);
                pcmServer.sendKeyboard(32, 1, 0);
                std::cout << "🔘 Press AI key, send it to client" << std::endl;
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
                        pcmServer.sendKeyboard(32, 1, 2);
                        recording = true;
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
                    std::cout << "🖱️ Click AI key, send it to client" << std::endl;
                    pcmServer.sendKeyboard(32, 0, 0);
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
                pcmServer.sendKeyboard(keyCode, 1, 0);
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
}

int main()
{
    // === start TCP server ===
    if (!pcmServer.start()) {
        std::cerr << "Failed to start PCM TCP server.\n";
        return -1;
    }
    std::cout << "Start TCP server " << std::endl;
    
    // === initialize HID Manager ===
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager) {
        std::cerr << "Failed to create IOHIDManager\n";
        return -1;
    }

    int vendorID = 0x248A;
    int productID_BT_Mouse = 0x8266;  // Bluetooth Mouse PID
    int productID_BT_KB = 0x8208;  // Bluetooth Keyboard PID
    int productID_USB = 0xCA10; // 2.4G PID

    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendorID);
    
    CFMutableDictionaryRef dictBTMouse = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef pidRefBTMouse = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID_BT_Mouse);
    CFDictionarySetValue(dictBTMouse, CFSTR(kIOHIDVendorIDKey), vidRef);
    CFDictionarySetValue(dictBTMouse, CFSTR(kIOHIDProductIDKey), pidRefBTMouse);
    
    CFMutableDictionaryRef dictBTKB = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef pidRefBTKB = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID_BT_KB);
    CFDictionarySetValue(dictBTKB, CFSTR(kIOHIDVendorIDKey), vidRef);
    CFDictionarySetValue(dictBTKB, CFSTR(kIOHIDProductIDKey), pidRefBTKB);

    CFMutableDictionaryRef dictUSB = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef pidRefUSB = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID_USB);
    CFDictionarySetValue(dictUSB, CFSTR(kIOHIDVendorIDKey), vidRef);
    CFDictionarySetValue(dictUSB, CFSTR(kIOHIDProductIDKey), pidRefUSB);

    // 放到数组里
    const void* dicts[3] = { dictBTMouse, dictBTKB, dictUSB };
    CFArrayRef matchingArray = CFArrayCreate(kCFAllocatorDefault, dicts, 3, &kCFTypeArrayCallBacks);

    // 设置匹配多个设备
    IOHIDManagerSetDeviceMatchingMultiple(hidManager, matchingArray);

    // 清理
    CFRelease(vidRef);
    CFRelease(pidRefBTMouse);
    CFRelease(pidRefBTKB);
    CFRelease(pidRefUSB);
    CFRelease(dictBTMouse);
    CFRelease(dictBTKB);
    CFRelease(dictUSB);
    
    // register device connect/remove callbacks
    IOHIDManagerRegisterDeviceMatchingCallback(hidManager, DeviceConnectedCallback, nullptr);
    IOHIDManagerRegisterDeviceRemovalCallback(hidManager, DeviceRemovedCallback, nullptr);
    CFRelease(matchingArray);

    IOHIDManagerRegisterInputValueCallback(hidManager, HandleInput, nullptr);
    IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        std::cerr << "Failed to open HID Manager\n";
        return -1;
    }

    std::cout << "Listening for HID input and BLE audio...\n";
    
    CFRunLoopRun();

    // ==== Terminate and cleanup ===
    if (pcmFile.is_open()) {
        pcmFile.close();
    }
    
    pcmServer.stop(); // stop TCP server

    CFRelease(hidManager);
    
    return 0;
}
