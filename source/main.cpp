#include <iostream>
#include "../header/server.h"

int main(int argc, char *argv[]){
    int port = 6379;
    if(argc >= 2) port = std::stoi(argv[1]); // stoi - converts string to signed integer
    // Sets port to a specific port if argument count is greater than 2 else default port is 6389.
    
    RedisServer server(port);
    
    return 0;
}