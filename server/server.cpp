#include "server.h"
#include "cmd_handler.h"
#include "redis_database.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <vector>
#include <thread>
#include <cstring>
#include <signal.h>

static RedisServer* globalServer = nullptr;

void signalHandler(int signum) {
    if (globalServer) {
        std :: cout << "\nCaught signal " << signum << ", shutting down ...\n";
        globalServer -> shutdown();
    }
    exit(signum);
}

void RedisServer :: setupSignalHandler() {
    signal(SIGINT, signalHandler);
}

RedisServer::RedisServer(int port): port(port), server_socket(-1), running(false){
    globalServer = this;
    setupSignalHandler();
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

    std::vector<std::thread> threads;
    RedisCommandHandler cmd_Handler;

    //Server now enters a loop to accept and process each client.
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

        threads.emplace_back([client_socket, &cmd_Handler](){
            char buffer[1024];
            while (true){
                memset(buffer, 0, sizeof(buffer));
                int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                if (bytes <= 0){
                    // std :: cout << "Client Disconnected\n";
                    break;
                }
                std :: string request(buffer, bytes);
                std :: cout << request;
                std :: string response = cmd_Handler.processCommand(request);
                std :: cout << response;
                send(client_socket, response.c_str(), response.size(), 0);
            }
            close(client_socket);
        });
        
    }

    for(auto& t : threads){
        if(t.joinable())
            t.join();
    }

    //Persistance before Shutdown
    if(!RedisDatabase::getInstance().dump("redisDB.rdb"))
        std :: cerr << "Error Dumping Database\n";
    else
        std :: cout << "Database Dumped to redisDB.rdb\n";

}
void RedisServer::shutdown(){
    running = false;
    if (server_socket != -1){
        //Dump database
        if(!RedisDatabase::getInstance().dump("redisDB.rdb"))
            std :: cerr << "Error Dumping Database\n";
        else
            std :: cout << "Database Dumped to redisDB.rdb\n";
        close(server_socket);
    }
    std::cout << "Server Shutting Down...!\n";
}