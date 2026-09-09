#include "../header/client.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

RedisClient *globalClient = nullptr;

RedisClient :: RedisClient(const std :: string &host, int port): server_IP(host), port(port), client_socket(-1){
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
    serverAddr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); //Default client address: 127.0.0.1 (current device address)
    if (connect(client_socket,(struct sockaddr*)&serverAddr,sizeof(serverAddr)) < 0) {
        std :: cerr << "Connection failed\n";
        return;
    }
    std :: cout << "Connect Successful!\n"; 
}