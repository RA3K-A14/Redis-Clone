#include "../header/server.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

static RedisServer* globalServer = nullptr;

RedisServer::RedisServer(int port): port(port), server_socket(-1), running(false){
    globalServer = this;
}

void RedisServer::run(){
    //Obtain a socket handle
    //AF_INET for IPv4
    //SOCK_STREAM for TCP
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0 ){
        std::cerr << "Error creating Socket for the server!!!\n";
        return;
    }
    //Setting socket option
    //2nd & 3rd arguments specifies which option to set
    //4th argument is the option value
    //If SO_REUSEADDR is not set to 1 server program cannot bind to the same IP:Port after a restart
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    //Binding to an address
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);  //Port (default : 6379)
    serverAddr.sin_addr.s_addr = INADDR_ANY;    //INADDR_ANY - represent wildcard address 0.0.0.0

    if (bind(server_socket,(struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0){
        std::cout << "Error binding server sockets!!!\n";
        close(server_socket);
        return;
    }
    //listen() changes the socket into a passive listening socket that can accept incoming connections
    if (listen(server_socket,10) < 0){
        std::cout << "Error listening server socket!!!\n";
        close(server_socket);
        return;
    }
    running = true;
    std::cout << "Redis Server listening on port: " << port << "\n";
    //Server should not enter a loop to accept and process each client.
    while(running){
        sockaddr_in clientAddr{};
        socklen_t clientSize = sizeof(clientAddr);
        int client_socket = accept(server_socket,(struct sockaddr*)&clientAddr, &clientSize);
        if(client_socket < 0){
            if(!running){
                break;
            }
            std :: cerr << "Error Accepting client\n";
            continue;
        }
        std::cout << "Client connected successfully\n";
        //...
    }
}
void RedisServer::shutdown(){
    running = false;
    if (server_socket != -1){
        close(server_socket);
    }
    std::cout << "Server Shutting Down...!\n";
}