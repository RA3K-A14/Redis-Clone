#include "../header/client.h"

RedisClient *globalClient = nullptr;

RedisClient :: RedisClient(const std :: string &host, int port): server_IP(host), port(port), client_socket(-1){
    globalClient = this;
}

void RedisClient :: connect(){}