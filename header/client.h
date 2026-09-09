#ifndef CLIENT_H
#define CLIENT_H

#include <string>

class RedisClient{
private:
    int client_socket;
    std :: string server_IP;
    int port;

public:
    RedisClient(const std :: string &host, int port);
    void connect_to_server();
};

#endif