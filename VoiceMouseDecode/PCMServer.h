//
//  PCMServer.h
//  VoiceMouseDecode
//
//  Created by Qianqian Zu on 2025/6/27.
//

#pragma once
#include <thread>
#include <atomic>
#include <netinet/in.h>

class PCMServer {
public:
    PCMServer(int port = 3395);
    ~PCMServer();

    bool start();
    void stop();
    void sendAudioPCM(uint8_t* data, size_t length);
    void sendKeyboard(uint16_t key, uint8_t state, uint16_t action_type);
    void sendDeviceConnect(std::string deviceInfo, uint8_t deviceType, uint8_t deviceMode, std::string deviceMACAddr);
    void sendDeviceDisconnect(std::string deviceInfo, uint8_t deviceType, uint8_t deviceMode);
    void setOnClientConnected(std::function<void()> callback) {
        onClientConnected = callback;
    }
    void sendStatusMessage(const std::string &msg);
    void onClientMessage(int clientFd, const std::string& msg);

private:
    void run();             // TCP监听线程
    void clientThread(int clientFd);
    std::vector<int> clients;
    std::mutex clientsMutex;

    int server_fd{-1};      // 服务端 socket
    int client_fd{-1};      // 唯一客户端 socket
    int port;

    std::thread serverThread;
    std::atomic<bool> running{false};

    // 权限管理
    std::unordered_map<int, bool> clientPermissions;
    std::mutex permMutex;

    // 权限相关辅助函数
    bool checkPermission(int clientFd);
    
    std::function<void()> onClientConnected;
};
