#include "client.h"
#include <iostream>
#include <algorithm>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

RedisClient *globalClient = nullptr;

RedisClient :: RedisClient(const std :: string &host, int port): client_socket(-1), server_IP(host), port(port){
    globalClient = this;
}

void RedisClient :: connect_to_server(){
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0 ){
        std::cerr << "Error creating Socket for the client!!!\n";
        return;
    }
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);  //Port (default : 6379)
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //127.0.0.1 - current device address
    if (connect(client_socket,(struct sockaddr*)&serverAddr,sizeof(serverAddr)) < 0) {
        std :: cerr << "Connection failed\n";
        return;
    }
    std :: cout << "Connect Successful!\n"; 
    while (true){
        std :: string request;
        std :: cout << "redis> ";
        std :: getline(std :: cin, request);
        std :: transform(request.begin(),request.end(),request.begin(), ::tolower);
        if (request == "quit"){
            std :: cout << "Quitting client ...\n";
            break;
        }
        send(client_socket, request.c_str(), request.size(), 0);

        char buffer[1024] = {0};
        int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0){
            std :: cout << "Client Disconnected\n";
            break;
        }
        buffer[bytes] = '\0';
        std :: cout << buffer << std :: endl;
    }
    close(client_socket);
}