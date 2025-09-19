//
//  PCMServer.cpp
//  VoiceMouseDecode
//
//  Created by Qianqian Zu on 2025/6/27.
//

#include "PCMServer.h"
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "json.hpp"
#include "base64.h"
#include <sqlite3.h>
#include <cstdlib>
#include <ApplicationServices/ApplicationServices.h>


using json = nlohmann::json;

PCMServer::PCMServer(int port) : server_fd(-1), client_fd(-1), port(port), running(false) {}

PCMServer::~PCMServer() {
    stop();
}

bool PCMServer::start() {
    running = true;
    serverThread = std::thread(&PCMServer::run, this);
    return true;
}

void PCMServer::stop() {
    running = false;
    if (server_fd != -1) close(server_fd);
    if (client_fd != -1) close(client_fd);
    if (serverThread.joinable()) serverThread.join();
}

void PCMServer::clientThread(int clientFd) {
    std::cout << "into client thread" << std::endl;
    char buffer[1024];
    while (running) {
        ssize_t n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break; // 连接关闭或出错
        buffer[n] = '\0';
        std::string msg(buffer);
        onClientMessage(clientFd, msg); // 处理消息
    }

    // 客户端断开处理
    close(clientFd);
}

void PCMServer::run() {
    struct sockaddr_in address{};
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        return;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen failed");
        return;
    }

    std::cout << "Waiting for client to connect on port " << port << "...\n";
    client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (client_fd < 0) {
        perror("accept failed");
        return;
    }

    std::cout << "Client connected!\n";
    std::thread(&PCMServer::clientThread, this, client_fd).detach();

    while (running) {
        usleep(100000); // idle loop, real data is sent from sendPCM
    }
}

void PCMServer::sendAudioPCM(uint8_t* data, size_t length) {
    if (client_fd == -1 || data == nullptr || length == 0)
        return;

    try {
        // Base64 编码
        std::string base64_data = base64_encode(data, length);

        // 构建 JSON 对象
        json j = {
            {"type", "ON_VOICE_DATA"},
            {"status", "true"},
            {"data", {
                {"length", length},
                {"bytes", base64_data},
                {"bytes_len", base64_data.length()}
            }}
        };

        // 序列化 JSON 并加上分隔符
        std::string response = j.dump() + "|||";

        // 发送数据
        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent < 0) {
            perror("send failed");
        }
    } catch (const std::exception& e) {
        std::cerr << "[PCMServer::sendAudioPCM] Exception: " << e.what() << std::endl;
    }
}

void PCMServer::sendKeyboard(uint16_t key, uint8_t state, uint16_t action_type)
{
    if (client_fd == -1)
        return;
    try {
        json j = {
                {"type", "ON_AI_BUTTON_EVENT"},
                {"status", "true"},
                {"data", {
                    {"key",key},
                    {"state",state},
                    {"action_type",action_type},
                 }}
            };
        
        std::string response = j.dump() + "|||";
        
        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent < 0) {
            perror("send failed");
        }
    } catch (const std::exception& e) {
        std::cerr << "[PCMServer::sendKeyboard] Exception: " << e.what() << std::endl;
    }
}

void PCMServer::sendDeviceConnect(std::string deviceInfo, uint8_t deviceType, uint8_t deviceMode, std::string deviceMACAddr)
{
    if (client_fd == -1)
        return;
    try {
        json j = {
            {"type", "ON_HARDWARE_CONNECT"},
            {"status", "true"},
            {"data", {
                {"deviceId",deviceInfo},
                {"deviceType",deviceType},
                {"deviceMode",deviceMode},
                {"deviceMacAddress", deviceMACAddr},
            }}
        };
        
        std::string response = j.dump() + "|||";
        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent < 0) {
            perror("send failed");
        }
    } catch (const std::exception& e) {
        std::cerr << "[PCMServer::sendDeviceConnect] Exception: " << e.what() << std::endl;
    }
}

void PCMServer::sendDeviceDisconnect(std::string deviceInfo, uint8_t deviceType, uint8_t deviceMode)
{
    if (client_fd == -1)
        return;
    try {
        json j = {
            {"type", "ON_HARDWARE_DISCONNECT"},
            {"status", "true"},
            {"data", {
                {"deviceId",deviceInfo},
                {"deviceType",deviceType},
                {"deviceMode",deviceMode},
            }}
        };
        
        std::string response = j.dump() + "|||";
        ssize_t sent = send(client_fd, response.c_str(), response.size(), 0);
        if (sent < 0) {
            perror("send failed");
        }
    } catch (const std::exception& e) {
        std::cerr << "[PCMServer::sendDeviceDisconnect] Exception: " << e.what() << std::endl;
    }
}

bool PCMServer::checkPermission(int clientFd) {
    bool allowed = AXIsProcessTrusted();
    if (allowed) {
        std::cout << "✅ Accessibility permission granted" << std::endl;
    } else {
        std::cout << "❌ Accessibility permission denied" << std::endl;
        std::cout << "👉 Please goto System Settings -> Privacy & Security -> Accessbility, allow your application" << std::endl;
    }
    return allowed;
}


void PCMServer::onClientMessage(int clientFd, const std::string& msg) {
    if (msg == "CHECK_PERMISSIONS") {
        std::cout << "Receive CHECK_PERMISSIONS msg" << std::endl;
        bool authorized = checkPermission(clientFd);
        std::string reply = authorized ? "AUTHORIZED" : "DENIED";
        std::cout << reply << " the client" << std::endl;
        
        if (clientFd == -1)
            return;
        try {
            json j = {
                {"type", "ON_CHECK_PERMISSIONS"},
                {"status", "true"},
                {"data", {
                    {"permission",reply},
                }}
            };
            
            std::string response = j.dump() + "|||";
            ssize_t sent = send(clientFd, response.c_str(), response.size(), 0);
            if (sent < 0) {
                perror("send failed");
            }
        } catch (const std::exception& e) {
            std::cerr << "[PCMServer::sendCheckPermission] Exception: " << e.what() << std::endl;
        }
    }
}
