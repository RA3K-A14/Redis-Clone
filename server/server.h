#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <atomic>

class RedisServer{
private:
    int port;
    int server_socket;
    //atomic prevents multiple thread to read or write the same flag simultaneously.
    std::atomic<bool> running;
public:
    RedisServer(int port);
    void run();
    void shutdown();
};

#endif