#include "common.h"

void init_sockets() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

int readCross(int socket, char* buffer, const int BUFFER_SIZE) {
#ifdef _WIN32 
    return recv(socket, buffer, BUFFER_SIZE,0);
#elif         
    return read(socket, buffer, BUFFER_SIZE);
#endif        
}

void closeCross(int socket) {
#ifdef _WIN32
    closesocket(socket);  
#elif
    close(socket);      
#endif
}

std::string chop_by_delimiter(std::string &input, char delimiter) {
    size_t delimiterPos = input.find(delimiter);
    
    if (delimiterPos == std::string::npos) {
        std::string result = input;
        input.clear();
        return result;
    }
    
    std::string result = input.substr(0, delimiterPos);
    input.erase(0, delimiterPos + 1);
    result.erase(std::remove_if(result.begin(), result.end(), [](unsigned char c) { return std::isspace(c); }), result.end());

    return result;
}

std::string encrypt_decrypt(std::string data) {

    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^=  0xAA;// Simple XOR key
    }
    return data;
}

int inetPtonCross(sockaddr_in& service) {
#ifdef _WIN32 
    return InetPton(AF_INET, __TEXT("127.0.0.1"), &service.sin_addr);
#elif         
    return inet_pton(AF_INET, "127.0.0.1", &service.sin_addr);
#endif        
}
