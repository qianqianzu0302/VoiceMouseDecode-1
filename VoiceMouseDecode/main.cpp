#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOBluetooth/IOBluetooth.h>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <vector>
#include <cstring>
#include "sbc.h"
#include "PCMServer.h"
#include <time.h>
#include "denoise.h"
#include "hidapi.h"
#include <regex>


std::map<IOHIDDeviceRef, uint32_t> deviceUsagePage; // 保存设备和usagePage映射

//std::ofstream pcmFile;
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

// ====== 获取缓存文件路径 ======
std::string getCacheFilePath() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp"; // fallback

    std::string path = std::string(home) + "/Library/Application Support/voicemousedecode";
    std::filesystem::create_directories(path); // 确保目录存在
    return path + "/device_id.txt";
}

// ====== 文件缓存工具函数 ======
void saveMacStrToFile(const std::string& macStr) {
    std::string filename = getCacheFilePath();
    std::ofstream ofs(filename, std::ios::trunc); // 覆盖写
    if (ofs) {
        ofs << macStr;
        std::cout << "Saved DeviceID to " << filename << std::endl;
    } else {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
    }
}

std::string loadMacStrFromFile() {
    std::string filename = getCacheFilePath();
    std::ifstream ifs(filename);
    std::string macStr;
    if (ifs) {
        std::getline(ifs, macStr);
    }
    return macStr;
}

void deleteMacStrFile() {
    std::string filename = getCacheFilePath();
    if (std::remove(filename.c_str()) == 0) {
        std::cout << "Deleted cache file: " << filename << std::endl;
    }
}

std::map<IOHIDDeviceRef, std::string> deviceMap;
// device connect
IOHIDDeviceRef usbMouse = nullptr;
void DeviceConnectedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
    int pid = 0;
    if (pidRef) CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);

    if (pid == 0x8266) {
        std::cout << "✅ Bluetooth mouse connected" << std::endl;
        deviceUsagePage[device] = 0xFF12;
        std::string mac = getBluetoothMouseMac();
        if (!mac.empty()) {
            deviceMap[device] = mac;   // 只在有值时插入
            pcmServer.sendDeviceConnect(mac, 0, 5, mac);
        } else {
            std::cout << "⚠️ Could not find MAC for Bluetooth mouse" << std::endl;
        }
    }
    else if (pid == 0xCA10) {
        std::cout << "✅ 2.4G device connected" << std::endl;
        deviceUsagePage[device] = 0xFF02;
        
        //deviceMap[device] = "2.4G";
        // 保存 2.4G 鼠标设备引用
        usbMouse = device;
        
        // 先尝试从缓存读取 MAC
        std::string cachedMac = loadMacStrFromFile();
        if (!cachedMac.empty()) {
            std::cout << "📂 Loaded cached MAC: " << cachedMac << std::endl;
            deviceMap[device] = cachedMac;
            pcmServer.sendDeviceConnect(cachedMac, 0, 2, cachedMac);
            return;
        }
        
        // 发送初始化命令
        uint8_t command[4] = {5, 1, 0, 0}; // 第一个字节是 Report ID = 5
        IOReturn ret = IOHIDDeviceSetReport(device,
                                            kIOHIDReportTypeOutput,
                                            command[0],  // reportID
                                            command,
                                            sizeof(command)); // 包含 reportID

        if (ret == kIOReturnSuccess) {
            std::cout << "📤 Sent {5,1,0,0} to 2.4G mouse" << std::endl;
        } else {
            std::cerr << "❌ Failed to send command, IOReturn = 0x"
                      << std::hex << ret << std::endl;
        }
    }
    else if (pid == 0x8208) {
        std::cout << "✅ Bluetooth keyboard connected" << std::endl;
        // 键盘不处理音频，不放入 map
    }
}

void sendCurrentDevices() {
    for (auto &entry : deviceMap) {
        IOHIDDeviceRef device = entry.first;
        std::string name = entry.second;

        CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
        int pid = 0;
        if (pidRef) CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);

        if (pid == 0x8266) {
            pcmServer.sendDeviceConnect(name, 0, 5, name);   // 蓝牙鼠标
        } else if (pid == 0xCA10) {
            pcmServer.sendDeviceConnect(name, 0, 2, name);   // 2.4G
        } else if (pid == 0x8208) {
            // 键盘不处理音频，不发
        }
    }
}

// device removal
void DeviceRemovedCallback(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    CFTypeRef pidRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
    int pid = 0;
    if (pidRef) CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);

    if (pid == 0x8266) {
        std::cout << "✅ Bluetooth mouse disconnected" << std::endl;
        auto it = deviceMap.find(device);
        if (it != deviceMap.end()) {
            pcmServer.sendDeviceDisconnect(it->second, 0, 5);
            deviceMap.erase(it);
        } else {
            std::cout << "⚠️ MAC not found for disconnected device" << std::endl;
        }
    }
    else if (pid == 0xCA10) {
        std::cout << "✅ 2.4G device disconnected" << std::endl;
        auto it = deviceMap.find(device);
        if (it != deviceMap.end()) {
            pcmServer.sendDeviceDisconnect(it->second, 0, 2);
            deviceMap.erase(it);
            deleteMacStrFile();
        } else {
            std::cout << "⚠️ MAC not found for disconnected device" << std::endl;
        }
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
    uint8_t* data = (uint8_t*)IOHIDValueGetBytePtr(value);
    
    /*std::cout << "usagePage = 0x" << std::hex << usagePage << ", usage = 0x" << std::hex << usage << std::endl;
     std::cout << "Input data (len=" << std::dec << length << "): ";
     for (CFIndex i = 0; i < length; ++i) {
     std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
     }
     std::cout << std::endl;*/
    
    if (data[0] == 0x81 && data[1] == 0x1 && data[2] == 0x10)
    {
        //receive device info from 2.4G mouse
        std::ostringstream oss;

        for (int i = 3; i <= 8; ++i) {  // data[3]~data[8]
            if (i > 3) oss << ":";       // 分隔符
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
        }

        std::string macStr = oss.str();
        std::cout << "DeviceID string: " << macStr << std::endl;
        
        if (usbMouse) {
            deviceMap[usbMouse] = macStr;  // usbMouse 在 DeviceConnectedCallback 中保存
        }
        
        // ✅ 保存到文件
        saveMacStrToFile(macStr);
        
        pcmServer.sendDeviceConnect(macStr, 0, 2, macStr);
    }
    
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
                        /*if (!pcmFile.is_open())
                        {
                            pcmFile.open("audio_data_decoded.pcm", std::ios::binary | std::ios::trunc);
                            if (!pcmFile)
                            {
                                std::cerr << "❌ Can't open PCM file to write\n";
                                return;
                            }
                        }
                        
                        pcmFile.write(reinterpret_cast<const char*>(pcm_output), pcm_len);
                        pcmFile.flush();*/
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
    
    // ✅ 注册客户端连接回调
    pcmServer.setOnClientConnected([]() {
        std::cout << "📡 New TCP client connected, send current devices..." << std::endl;
        sendCurrentDevices();
    });
    
    // === initialize HID Manager ===
    IOHIDManagerRef hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager)
    {
        std::cerr << "Failed to create IOHIDManager\n";
        pcmServer.sendStatusMessage("HID_MANAGER_ERROR: Failed to create HID Manager");
    }
    else
    {
        IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
        if (ret != kIOReturnSuccess)
        {
            std::cerr << "❌ Failed to open HID Manager: Input Monitoring permission required\n";
            pcmServer.sendStatusMessage("HID_MANAGER_ERROR: Input Monitoring permission denied");
        }
        else
        {
            std::cout << "HID Manager opened successfully\n";
            
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
        }
    }

    /*IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        std::cerr << "Failed to open HID Manager\n";
        return -1;
    }*/

    std::cout << "Listening for HID input and BLE audio...\n";
    
    CFRunLoopRun();

    // ==== Terminate and cleanup ===
    /*if (pcmFile.is_open()) {
        pcmFile.close();
    }*/
    
    pcmServer.stop(); // stop TCP server

    CFRelease(hidManager);
    
    return 0;
}
