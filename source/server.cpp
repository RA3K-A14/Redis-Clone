#include "../header/server.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

static RedisServer* globalServer = nullptr;

RedisServer::RedisServer(int port): port(port), server_socket(-1), running(true){
    globalServer = this;
}

void RedisServer::run(){
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0 ){
        std::cerr << "Error creating Socket for the server!!!\n";
        return;
    }
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket,(struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0){
        std::cout << "Error binding server sockets!!!\n";
        return;
    }

    if (listen(server_socket,10) < 0){
        std::cout << "Error listening server socket!!!\n";
        return;
    }
    std::cout << "Redis Server listening on port: " << port << "\n";
}
void RedisServer::shutdown(){
    running = false;
    if (server_socket != -1){
        close(server_socket);
    }  
    std::cout << "Server Shutting Down...!\n";
}